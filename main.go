package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"purple_go/pkg/ast"
	"purple_go/pkg/codegen"
	"purple_go/pkg/compiler"
	"purple_go/pkg/eval"
	"purple_go/pkg/parser"
)

var (
	compileMode = flag.Bool("c", false, "Compile to C code instead of executing")
	interpMode  = flag.Bool("interp", false, "Use Go interpreter (legacy, slower)")
	outputFile  = flag.String("o", "", "Output file (default: stdout for -c, a.out for binary)")
	evalExpr    = flag.String("e", "", "Evaluate expression from command line")
	verbose     = flag.Bool("v", false, "Verbose output")
	runtimePath = flag.String("runtime", "", "Path to external runtime (auto-detected if not set)")
)

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Purple Go - Native Compiler with ASAP Memory Management\n\n")
		fmt.Fprintf(os.Stderr, "Usage: %s [options] [file.purple]\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, "\nExamples:\n")
		fmt.Fprintf(os.Stderr, "  %s -e '(+ 1 2)'              # Compile and run expression\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s -c -e '(+ 1 2)'           # Emit C code to stdout\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s program.purple            # Compile and run file\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s -c program.purple -o out.c # Compile file to C\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s -o prog program.purple    # Compile to binary 'prog'\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s --interp -e '(+ 1 2)'     # Use Go interpreter (legacy)\n", os.Args[0])
	}
	flag.Parse()

	// Auto-detect runtime path if not specified
	if *runtimePath == "" {
		*runtimePath = findRuntimePath()
	}

	var input string
	var err error

	if *evalExpr != "" {
		input = *evalExpr
	} else if flag.NArg() > 0 {
		filename := flag.Arg(0)
		data, err := os.ReadFile(filename)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading file: %v\n", err)
			os.Exit(1)
		}
		input = string(data)
	} else {
		// Read from stdin
		data, err := io.ReadAll(os.Stdin)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading stdin: %v\n", err)
			os.Exit(1)
		}
		input = string(data)
	}

	if strings.TrimSpace(input) == "" {
		// Interactive REPL mode
		runREPL()
		return
	}

	// Parse all expressions
	p := parser.New(input)
	exprs, err := p.ParseAll()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Parse error: %v\n", err)
		os.Exit(1)
	}

	if len(exprs) == 0 {
		fmt.Fprintf(os.Stderr, "No expressions to process\n")
		os.Exit(1)
	}

	if *interpMode {
		// Legacy Go interpreter (slower, for debugging)
		interpret(exprs)
	} else if *compileMode {
		// Emit C code to stdout or file
		emitCCode(exprs)
	} else {
		// Default: Native compilation and execution
		runNative(exprs)
	}
}

// findRuntimePath searches for the runtime directory
func findRuntimePath() string {
	// Check relative to executable
	exe, err := os.Executable()
	if err == nil {
		exeDir := filepath.Dir(exe)
		candidates := []string{
			filepath.Join(exeDir, "runtime"),
			filepath.Join(exeDir, "..", "runtime"),
		}
		for _, path := range candidates {
			if _, err := os.Stat(filepath.Join(path, "libpurple.a")); err == nil {
				return path
			}
		}
	}

	// Check relative to current directory
	if _, err := os.Stat("runtime/libpurple.a"); err == nil {
		return "runtime"
	}

	// Check relative to working directory
	wd, err := os.Getwd()
	if err == nil {
		path := filepath.Join(wd, "runtime")
		if _, err := os.Stat(filepath.Join(path, "libpurple.a")); err == nil {
			return path
		}
	}

	// Not found - will use embedded runtime
	return ""
}

func interpret(exprs []*ast.Value) {
	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)

	for _, expr := range exprs {
		if *verbose {
			fmt.Printf("Evaluating: %s\n", expr.String())
		}

		result := eval.Eval(expr, menv)
		if result != nil {
			if ast.IsCode(result) {
				fmt.Printf("Code: %s\n", result.Str)
			} else {
				fmt.Printf("Result: %s\n", result.String())
			}
		}
	}
}

// runNative compiles and executes expressions natively (default mode)
func runNative(exprs []*ast.Value) {
	var comp *compiler.Compiler

	if *runtimePath != "" {
		comp = compiler.NewWithExternalRuntime(*runtimePath)
		if *verbose {
			fmt.Fprintf(os.Stderr, "Using external runtime: %s\n", *runtimePath)
		}
	} else {
		comp = compiler.New()
		if *verbose {
			fmt.Fprintf(os.Stderr, "Using embedded runtime\n")
		}
	}

	// If output file specified, compile to binary (don't run)
	if *outputFile != "" {
		binPath, err := comp.CompileToBinary(exprs, *outputFile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Compile error: %v\n", err)
			os.Exit(1)
		}
		if *verbose {
			fmt.Fprintf(os.Stderr, "Binary written to %s\n", binPath)
		}
		return
	}

	// Compile to temp binary and execute
	tmpDir, err := os.MkdirTemp("", "purple_run_")
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error creating temp dir: %v\n", err)
		os.Exit(1)
	}
	defer os.RemoveAll(tmpDir)

	binPath := filepath.Join(tmpDir, "program")
	_, err = comp.CompileToBinary(exprs, binPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Compile error: %v\n", err)
		os.Exit(1)
	}

	// Execute the binary
	cmd := exec.Command(binPath)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			os.Exit(exitErr.ExitCode())
		}
		fmt.Fprintf(os.Stderr, "Execution error: %v\n", err)
		os.Exit(1)
	}
}

// emitCCode generates C code and writes to stdout or file
func emitCCode(exprs []*ast.Value) {
	var comp *compiler.Compiler

	if *runtimePath != "" {
		comp = compiler.NewWithExternalRuntime(*runtimePath)
	} else {
		comp = compiler.New()
	}

	code, err := comp.CompileProgram(exprs)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Compile error: %v\n", err)
		os.Exit(1)
	}

	if *outputFile != "" {
		if err := os.WriteFile(*outputFile, []byte(code), 0644); err != nil {
			fmt.Fprintf(os.Stderr, "Error writing output: %v\n", err)
			os.Exit(1)
		}
		if *verbose {
			fmt.Fprintf(os.Stderr, "C code written to %s\n", *outputFile)
		}
	} else {
		fmt.Print(code)
	}
}

func compileToC(exprs []*ast.Value) {
	var output io.Writer = os.Stdout
	if *outputFile != "" {
		f, err := os.Create(*outputFile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error creating output file: %v\n", err)
			os.Exit(1)
		}
		defer f.Close()
		output = f
	}

	// Evaluate expressions to get code
	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)

	// Prepare code generator early so function summaries can be recorded.
	gen := codegen.NewCodeGenerator(output)
	codegen.SetGlobalCodeGenerator(gen)

	var codeExprs []*ast.Value
	for _, expr := range exprs {
		result := eval.Eval(expr, menv)
		if result != nil {
			codeExprs = append(codeExprs, result)
		}
	}

	// Generate complete C program
	gen.GenerateProgram(codeExprs)

	if *outputFile != "" && *verbose {
		fmt.Fprintf(os.Stderr, "Generated C code written to %s\n", *outputFile)
	}
}

func runREPL() {
	fmt.Println("Purple Native REPL - ASAP Memory Management")
	fmt.Println()

	// Check GCC availability for JIT
	_, gccErr := exec.LookPath("gcc")
	if gccErr != nil {
		fmt.Println("  Error: gcc not found - REPL requires gcc for native compilation")
		os.Exit(1)
	}

	// Check runtime
	if *runtimePath != "" {
		fmt.Printf("  Runtime: %s\n", *runtimePath)
	} else {
		fmt.Println("  Runtime: embedded")
	}
	fmt.Println()
	fmt.Println("Type 'help' for commands, 'quit' to exit")
	fmt.Println()

	// Create persistent temp directory for JIT
	tmpDir, err := os.MkdirTemp("", "purple_repl_")
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error creating temp dir: %v\n", err)
		os.Exit(1)
	}
	defer os.RemoveAll(tmpDir)

	// Track definitions for multi-line sessions
	var definitions []string
	jitCounter := 0
	showCCode := false

	scanner := bufio.NewScanner(os.Stdin)
	for {
		if showCCode {
			fmt.Print("purple(c)> ")
		} else {
			fmt.Print("purple> ")
		}

		if !scanner.Scan() {
			break
		}

		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		// Handle commands first (before parsing)
		switch line {
		case "quit", "exit":
			fmt.Println("Goodbye!")
			return
		case "code":
			showCCode = !showCCode
			if showCCode {
				fmt.Println("C code display ON")
			} else {
				fmt.Println("C code display OFF")
			}
			continue
		case "clear":
			definitions = nil
			fmt.Println("Definitions cleared")
			continue
		case "defs":
			if len(definitions) == 0 {
				fmt.Println("No definitions")
			} else {
				fmt.Println("Current definitions:")
				for _, d := range definitions {
					fmt.Printf("  %s\n", d)
				}
			}
			continue
		case "help":
			printREPLHelp()
			continue
		}

		// Skip if it's just a bare word (not a valid expression)
		if !strings.HasPrefix(line, "(") && !strings.HasPrefix(line, "'") {
			fmt.Printf("Unknown command: %s (use 'help' for commands)\n", line)
			continue
		}

		// Parse the expression
		p := parser.New(line)
		expr, err := p.Parse()
		if err != nil {
			fmt.Printf("Parse error: %v\n", err)
			continue
		}

		if expr == nil {
			continue
		}

		// Check if it's a definition
		isDefine := ast.IsCell(expr) && ast.IsSym(expr.Car) && expr.Car.Str == "define"
		if isDefine {
			definitions = append(definitions, line)
			fmt.Println("Defined")
			continue
		}

		// Build full program with definitions + expression
		var fullInput strings.Builder
		for _, def := range definitions {
			fullInput.WriteString(def)
			fullInput.WriteString("\n")
		}
		fullInput.WriteString(line)

		// Parse the full program
		fullParser := parser.New(fullInput.String())
		exprs, err := fullParser.ParseAll()
		if err != nil {
			fmt.Printf("Parse error: %v\n", err)
			continue
		}

		// Compile
		var comp *compiler.Compiler
		if *runtimePath != "" {
			comp = compiler.NewWithExternalRuntime(*runtimePath)
		} else {
			comp = compiler.New()
		}

		code, err := comp.CompileProgram(exprs)
		if err != nil {
			fmt.Printf("Compile error: %v\n", err)
			continue
		}

		if showCCode {
			fmt.Println("--- C code ---")
			fmt.Print(code)
			fmt.Println("--- end ---")
		}

		// Compile to binary
		jitCounter++
		binPath := filepath.Join(tmpDir, fmt.Sprintf("repl_%d", jitCounter))
		srcPath := binPath + ".c"

		if err := os.WriteFile(srcPath, []byte(code), 0644); err != nil {
			fmt.Printf("Error writing source: %v\n", err)
			continue
		}

		var gccCmd *exec.Cmd
		if *runtimePath != "" {
			includePath := filepath.Join(*runtimePath, "include")
			gccCmd = exec.Command("gcc",
				"-std=c99", "-pthread", "-O2",
				"-I", includePath,
				"-o", binPath,
				srcPath,
				"-L", *runtimePath, "-lpurple",
			)
		} else {
			gccCmd = exec.Command("gcc", "-std=c99", "-pthread", "-O2", "-o", binPath, srcPath)
		}

		output, err := gccCmd.CombinedOutput()
		if err != nil {
			fmt.Printf("Compile error: %v\n%s", err, output)
			continue
		}

		// Execute
		runCmd := exec.Command(binPath)
		runCmd.Stdout = os.Stdout
		runCmd.Stderr = os.Stderr
		runCmd.Run()
	}
}

func printREPLHelp() {
	fmt.Println("Commands:")
	fmt.Println("  quit     - exit the REPL")
	fmt.Println("  code     - toggle C code display")
	fmt.Println("  defs     - show current definitions")
	fmt.Println("  clear    - clear all definitions")
	fmt.Println("  help     - show this help")
	fmt.Println()
	fmt.Println("Language:")
	fmt.Println("  (define name value)     - define a variable")
	fmt.Println("  (define (f x) body)     - define a function")
	fmt.Println("  (lambda (x) body)       - anonymous function")
	fmt.Println("  (let ((x val)) body)    - local binding")
	fmt.Println("  (if cond then else)     - conditional")
	fmt.Println("  (do expr1 expr2 ...)    - sequence")
	fmt.Println("  (quote x) or 'x         - quote expression")
	fmt.Println()
	fmt.Println("Primitives:")
	fmt.Println("  Arithmetic: + - * / %")
	fmt.Println("  Comparison: < > <= >= = eq?")
	fmt.Println("  Lists: cons car cdr null? pair? list")
	fmt.Println("  I/O: display print newline")
	fmt.Println()
	fmt.Println("Examples:")
	fmt.Println("  (+ 1 2)                         => 3")
	fmt.Println("  (define (fib n) (if (<= n 1) n (+ (fib (- n 1)) (fib (- n 2)))))")
	fmt.Println("  (fib 10)                        => 55")
	fmt.Println("  (map (lambda (x) (* x 2)) '(1 2 3)) => (2 4 6)")
}
