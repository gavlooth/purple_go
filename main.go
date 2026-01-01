package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"

	"purple_go/pkg/ast"
	"purple_go/pkg/codegen"
	"purple_go/pkg/compiler"
	"purple_go/pkg/eval"
	"purple_go/pkg/jit"
	"purple_go/pkg/memory"
	"purple_go/pkg/parser"
)

var (
	compileMode = flag.Bool("c", false, "Compile to C code instead of interpreting")
	nativeMode  = flag.Bool("native", false, "Compile AST directly to a native binary")
	outputFile  = flag.String("o", "", "Output file (default: stdout)")
	evalExpr    = flag.String("e", "", "Evaluate expression from command line")
	verbose     = flag.Bool("v", false, "Verbose output")
)

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Purple Go - ASAP Memory Management Compiler\n\n")
		fmt.Fprintf(os.Stderr, "Usage: %s [options] [file.purple]\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, "\nExamples:\n")
		fmt.Fprintf(os.Stderr, "  %s -e '(+ 1 2)'              # Evaluate expression\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s -c -e '(lift 42)'         # Compile to C\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s program.purple            # Run file\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s -c program.purple -o out.c # Compile file to C\n", os.Args[0])
	}
	flag.Parse()

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

	if *nativeMode {
		// Native compilation: AST -> C -> binary (or emit C with -c)
		compileNative(exprs)
	} else if *compileMode {
		// Staged compile to C (via eval)
		compileToC(exprs)
	} else {
		// Interpret
		interpret(exprs)
	}
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

func compileNative(exprs []*ast.Value) {
	comp := compiler.New()

	if *compileMode {
		// --native -c: emit C to stdout or file
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
				fmt.Fprintf(os.Stderr, "Generated C code written to %s\n", *outputFile)
			}
		} else {
			fmt.Print(code)
		}
		return
	}

	// --native without -c: compile to binary
	output := *outputFile
	if output == "" {
		output = "a.out"
	}

	binPath, err := comp.CompileToBinary(exprs, output)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Native compile error: %v\n", err)
		os.Exit(1)
	}

	if *verbose {
		fmt.Fprintf(os.Stderr, "Native binary written to %s\n", binPath)
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
	fmt.Println("Purple Go REPL - Tower of Interpreters with ASAP Memory Management")
	fmt.Println()

	// Check JIT availability
	j := jit.Get()
	if j.IsAvailable() {
		fmt.Println("  JIT: enabled (gcc found)")
	} else {
		fmt.Println("  JIT: disabled (gcc not found)")
	}
	fmt.Println()
	fmt.Println("Type 'help' for commands, 'quit' to exit")
	fmt.Println()

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)
	compiling := false

	scanner := bufio.NewScanner(os.Stdin)
	for {
		if compiling {
			fmt.Print("purple(compile)> ")
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

		switch line {
		case "quit", "exit":
			fmt.Println("Goodbye!")
			return
		case "compile":
			compiling = !compiling
			if compiling {
				fmt.Println("Compile mode ON - expressions will generate C code")
			} else {
				fmt.Println("Compile mode OFF - expressions will be interpreted")
			}
			continue
		case "macros":
			fmt.Println("Registered macros: (use defmacro to define, mcall to call)")
			continue
		case "help":
			printREPLHelp()
			continue
		case "runtime":
			registry := codegen.NewTypeRegistry()
			registry.InitDefaultTypes()

			// Also include memory management runtimes
			gen := codegen.NewRuntimeGenerator(os.Stdout, registry)
			gen.GenerateAll()

			sccGen := memory.NewSCCGenerator(os.Stdout)
			sccGen.GenerateSCCRuntime()
			sccGen.GenerateSCCDetection()

			deferredGen := memory.NewDeferredGenerator(os.Stdout)
			deferredGen.GenerateDeferredRuntime()

			arenaGen := memory.NewArenaGenerator(os.Stdout)
			arenaGen.GenerateArenaRuntime()
			continue
		}

		p := parser.New(line)
		expr, err := p.Parse()
		if err != nil {
			fmt.Printf("Parse error: %v\n", err)
			continue
		}

		if expr == nil {
			continue
		}

		result := eval.Eval(expr, menv)
		if result != nil {
			if ast.IsError(result) {
				fmt.Printf("Error: %s\n", result.Str)
			} else if ast.IsCode(result) {
				if compiling {
					fmt.Println("Generated C:")
					fmt.Println(result.Str)
				} else {
					fmt.Printf("Code: %s\n", result.Str)
				}
			} else {
				fmt.Printf("=> %s\n", result.String())
			}
		}
	}
}

func printREPLHelp() {
	fmt.Println("Commands:")
	fmt.Println("  quit     - exit the REPL")
	fmt.Println("  compile  - toggle compile mode (generate C code)")
	fmt.Println("  macros   - list defined macros")
	fmt.Println("  runtime  - print C runtime")
	fmt.Println("  help     - show this help")
	fmt.Println()
	fmt.Println("Special Forms:")
	fmt.Println("  (lambda (x) body)       - create function")
	fmt.Println("  (lambda self (x) body)  - recursive function")
	fmt.Println("  (let ((x val)) body)    - local binding")
	fmt.Println("  (letrec ((f fn)) body)  - recursive binding")
	fmt.Println("  (if cond then else)     - conditional")
	fmt.Println("  (match val (pat body)...) - pattern matching")
	fmt.Println("  (quote x) or 'x         - quote expression")
	fmt.Println("  (quasiquote x)          - quasiquote with ,unquote")
	fmt.Println()
	fmt.Println("Staging (Tower of Interpreters):")
	fmt.Println("  (lift val)              - lift value to code")
	fmt.Println("  (run code)              - execute code (JIT if available)")
	fmt.Println("  (EM expr)               - evaluate at parent meta-level")
	fmt.Println("  (shift n expr)          - go up n levels")
	fmt.Println("  (meta-level)            - get current tower level")
	fmt.Println("  (clambda (x) body)      - compile lambda")
	fmt.Println()
	fmt.Println("Handler Customization:")
	fmt.Println("  (get-meta 'name)        - get handler by name")
	fmt.Println("  (set-meta! 'name fn)    - install handler")
	fmt.Println("  (with-handlers ((name fn)) body)")
	fmt.Println("  (default-handler 'name arg)")
	fmt.Println()
	fmt.Println("Macros:")
	fmt.Println("  (defmacro name (params) body scope)")
	fmt.Println("  (mcall macro-name args...)")
	fmt.Println("  (macroexpand expr)      - expand without eval")
	fmt.Println()
	fmt.Println("Examples:")
	fmt.Println("  (+ 1 2)                 => 3")
	fmt.Println("  (map (lambda (x) (* x 2)) '(1 2 3)) => (2 4 6)")
	fmt.Println("  (lift 42)               => Code: mk_int(42)")
	fmt.Println("  (defmacro inc (x) `(+ 1 ,x) (mcall inc 5)) => 6")
}
