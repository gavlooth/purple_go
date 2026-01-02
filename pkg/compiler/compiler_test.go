package compiler

import (
	"os"
	"os/exec"
	"strings"
	"testing"

	"purple_go/pkg/parser"
)

// skipIfNoGCC skips the test if gcc is not available
func skipIfNoGCC(t *testing.T) {
	if _, err := exec.LookPath("gcc"); err != nil {
		t.Skip("gcc not available")
	}
}

// skipIfNoRuntime skips the test if the runtime library is not available
func skipIfNoRuntime(t *testing.T) string {
	// Try to find runtime
	candidates := []string{
		"../../runtime",
		"../../../runtime",
		"runtime",
	}
	for _, path := range candidates {
		if _, err := os.Stat(path + "/libpurple.a"); err == nil {
			return path
		}
	}
	t.Skip("runtime library not found")
	return ""
}

func TestCompileProgram_BasicExpressions(t *testing.T) {
	tests := []struct {
		name   string
		input  string
		expect string
	}{
		{"int_literal", "(+ 1 2)", "Result: 3"},
		{"mul", "(* 6 7)", "Result: 42"},
		{"comparison", "(if (< 3 5) 100 200)", "Result: 100"},
		{"let_binding", "(let ((x 10) (y 5)) (- x y))", "Result: 5"},
		{"nested_let", "(let ((x 5)) (let ((y (* x 2))) (+ x y)))", "Result: 15"},
	}

	skipIfNoGCC(t)
	runtimePath := skipIfNoRuntime(t)

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			comp := NewWithExternalRuntime(runtimePath)

			p := parser.New(tc.input)
			exprs, err := p.ParseAll()
			if err != nil {
				t.Fatalf("Parse error: %v", err)
			}

			code, err := comp.CompileProgram(exprs)
			if err != nil {
				t.Fatalf("CompileProgram error: %v", err)
			}

			if !strings.Contains(code, "#include <purple.h>") {
				t.Error("Expected external runtime include")
			}
			if !strings.Contains(code, "int main(void)") {
				t.Error("Expected main function")
			}
		})
	}
}

func TestCompileToBinary_Arithmetic(t *testing.T) {
	skipIfNoGCC(t)
	runtimePath := skipIfNoRuntime(t)

	tests := []struct {
		name   string
		input  string
		expect string
	}{
		{"add", "(+ 1 2)", "Result: 3"},
		{"sub", "(- 10 3)", "Result: 7"},
		{"mul", "(* 6 7)", "Result: 42"},
		{"div", "(/ 100 5)", "Result: 20"},
		{"mod", "(% 17 5)", "Result: 2"},
		{"nested", "(+ (* 3 4) (- 10 5))", "Result: 17"},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			comp := NewWithExternalRuntime(runtimePath)

			p := parser.New(tc.input)
			exprs, err := p.ParseAll()
			if err != nil {
				t.Fatalf("Parse error: %v", err)
			}

			tmpFile, err := os.CreateTemp("", "purple_test_*")
			if err != nil {
				t.Fatalf("CreateTemp error: %v", err)
			}
			tmpPath := tmpFile.Name()
			tmpFile.Close()
			defer os.Remove(tmpPath)

			_, err = comp.CompileToBinary(exprs, tmpPath)
			if err != nil {
				t.Fatalf("CompileToBinary error: %v", err)
			}

			out, err := exec.Command(tmpPath).CombinedOutput()
			if err != nil {
				t.Fatalf("Execution error: %v\n%s", err, out)
			}

			if !strings.Contains(string(out), tc.expect) {
				t.Errorf("Expected %q, got %q", tc.expect, string(out))
			}
		})
	}
}

func TestCompileToBinary_Conditionals(t *testing.T) {
	skipIfNoGCC(t)
	runtimePath := skipIfNoRuntime(t)

	tests := []struct {
		name   string
		input  string
		expect string
	}{
		{"if_true", "(if (< 3 5) 100 200)", "Result: 100"},
		{"if_false", "(if (> 3 5) 100 200)", "Result: 200"},
		{"nested_if", "(if (< 1 2) (if (< 3 4) 10 20) 30)", "Result: 10"},
		{"and_true", "(if (and (< 1 2) (< 3 4)) 1 0)", "Result: 1"},
		{"and_false", "(if (and (< 1 2) (> 3 4)) 1 0)", "Result: 0"},
		{"or_true", "(if (or (< 1 2) (> 3 4)) 1 0)", "Result: 1"},
		{"or_false", "(if (or (> 1 2) (> 3 4)) 1 0)", "Result: 0"},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			comp := NewWithExternalRuntime(runtimePath)

			p := parser.New(tc.input)
			exprs, err := p.ParseAll()
			if err != nil {
				t.Fatalf("Parse error: %v", err)
			}

			tmpFile, err := os.CreateTemp("", "purple_test_*")
			if err != nil {
				t.Fatalf("CreateTemp error: %v", err)
			}
			tmpPath := tmpFile.Name()
			tmpFile.Close()
			defer os.Remove(tmpPath)

			_, err = comp.CompileToBinary(exprs, tmpPath)
			if err != nil {
				t.Fatalf("CompileToBinary error: %v", err)
			}

			out, err := exec.Command(tmpPath).CombinedOutput()
			if err != nil {
				t.Fatalf("Execution error: %v\n%s", err, out)
			}

			if !strings.Contains(string(out), tc.expect) {
				t.Errorf("Expected %q, got %q", tc.expect, string(out))
			}
		})
	}
}

func TestCompileToBinary_Functions(t *testing.T) {
	skipIfNoGCC(t)
	runtimePath := skipIfNoRuntime(t)

	tests := []struct {
		name   string
		input  string
		expect string
	}{
		{"lambda", "((lambda (x) (* x x)) 7)", "Result: 49"},
		{"define_func", "(define (square x) (* x x)) (square 8)", "Result: 64"},
		{"define_recursive", "(define (fact n) (if (< n 2) 1 (* n (fact (- n 1))))) (fact 10)", "Result: 3628800"},
		{"higher_order", "(define (apply-twice f x) (f (f x))) (define (add1 x) (+ x 1)) (apply-twice add1 5)", "Result: 7"},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			comp := NewWithExternalRuntime(runtimePath)

			p := parser.New(tc.input)
			exprs, err := p.ParseAll()
			if err != nil {
				t.Fatalf("Parse error: %v", err)
			}

			tmpFile, err := os.CreateTemp("", "purple_test_*")
			if err != nil {
				t.Fatalf("CreateTemp error: %v", err)
			}
			tmpPath := tmpFile.Name()
			tmpFile.Close()
			defer os.Remove(tmpPath)

			_, err = comp.CompileToBinary(exprs, tmpPath)
			if err != nil {
				t.Fatalf("CompileToBinary error: %v", err)
			}

			out, err := exec.Command(tmpPath).CombinedOutput()
			if err != nil {
				t.Fatalf("Execution error: %v\n%s", err, out)
			}

			if !strings.Contains(string(out), tc.expect) {
				t.Errorf("Expected %q, got %q", tc.expect, string(out))
			}
		})
	}
}

func TestCompileToBinary_Closures(t *testing.T) {
	skipIfNoGCC(t)
	runtimePath := skipIfNoRuntime(t)

	tests := []struct {
		name   string
		input  string
		expect string
	}{
		{
			"closure_capture",
			"(let ((x 10)) ((lambda (y) (+ x y)) 5))",
			"Result: 15",
		},
		{
			"nested_closure",
			"(let ((x 5)) (let ((y 3)) ((lambda (z) (+ x (+ y z))) 2)))",
			"Result: 10",
		},
		{
			"make_adder",
			"(define (make-adder n) (lambda (x) (+ x n))) ((make-adder 10) 5)",
			"Result: 15",
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			comp := NewWithExternalRuntime(runtimePath)

			p := parser.New(tc.input)
			exprs, err := p.ParseAll()
			if err != nil {
				t.Fatalf("Parse error: %v", err)
			}

			tmpFile, err := os.CreateTemp("", "purple_test_*")
			if err != nil {
				t.Fatalf("CreateTemp error: %v", err)
			}
			tmpPath := tmpFile.Name()
			tmpFile.Close()
			defer os.Remove(tmpPath)

			_, err = comp.CompileToBinary(exprs, tmpPath)
			if err != nil {
				t.Fatalf("CompileToBinary error: %v", err)
			}

			out, err := exec.Command(tmpPath).CombinedOutput()
			if err != nil {
				t.Fatalf("Execution error: %v\n%s", err, out)
			}

			if !strings.Contains(string(out), tc.expect) {
				t.Errorf("Expected %q, got %q", tc.expect, string(out))
			}
		})
	}
}

func TestCompileToBinary_Quote(t *testing.T) {
	skipIfNoGCC(t)
	runtimePath := skipIfNoRuntime(t)

	comp := NewWithExternalRuntime(runtimePath)

	// Test that quoted expressions compile (result is a pair/list)
	input := "(car (quote (1 2 3)))"
	p := parser.New(input)
	exprs, err := p.ParseAll()
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}

	tmpFile, err := os.CreateTemp("", "purple_test_*")
	if err != nil {
		t.Fatalf("CreateTemp error: %v", err)
	}
	tmpPath := tmpFile.Name()
	tmpFile.Close()
	defer os.Remove(tmpPath)

	_, err = comp.CompileToBinary(exprs, tmpPath)
	if err != nil {
		t.Fatalf("CompileToBinary error: %v", err)
	}

	out, err := exec.Command(tmpPath).CombinedOutput()
	if err != nil {
		t.Fatalf("Execution error: %v\n%s", err, out)
	}

	if !strings.Contains(string(out), "Result: 1") {
		t.Errorf("Expected 'Result: 1', got %q", string(out))
	}
}

func TestCompiler_EmbeddedRuntime(t *testing.T) {
	skipIfNoGCC(t)

	// Test embedded runtime (no external library)
	comp := New()

	input := "(+ 1 2)"
	p := parser.New(input)
	exprs, err := p.ParseAll()
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}

	code, err := comp.CompileProgram(exprs)
	if err != nil {
		t.Fatalf("CompileProgram error: %v", err)
	}

	// Embedded runtime should include the full runtime, not just header
	if strings.Contains(code, "#include <purple.h>") {
		t.Error("Embedded runtime should not include external header")
	}
	if !strings.Contains(code, "typedef struct Obj Obj") {
		t.Error("Embedded runtime should include Obj definition")
	}
}

func TestCompiler_CCodeGeneration(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		expects []string
	}{
		{
			"int_literal",
			"42",
			[]string{"mk_int(42)"},
		},
		{
			"arithmetic",
			"(+ 1 2)",
			[]string{"prim_add"},
		},
		{
			"let_binding",
			"(let ((x 5)) x)",
			[]string{"v_x_"},
		},
		{
			"lambda",
			"(lambda (x) x)",
			[]string{"_lambda_", "mk_closure"},
		},
		{
			"if_expr",
			"(if t 1 2)",
			[]string{"is_truthy"},
		},
	}

	runtimePath := skipIfNoRuntime(t)

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			comp := NewWithExternalRuntime(runtimePath)

			p := parser.New(tc.input)
			exprs, err := p.ParseAll()
			if err != nil {
				t.Fatalf("Parse error: %v", err)
			}

			code, err := comp.CompileProgram(exprs)
			if err != nil {
				t.Fatalf("CompileProgram error: %v", err)
			}

			for _, expect := range tc.expects {
				if !strings.Contains(code, expect) {
					t.Errorf("Expected code to contain %q", expect)
				}
			}
		})
	}
}
