package eval

import (
	"strings"
	"testing"

	"purple_go/pkg/ast"
	"purple_go/pkg/parser"
)

func evalString(input string) *ast.Value {
	p := parser.New(input)
	expr, err := p.Parse()
	if err != nil {
		return nil
	}
	return Run(expr)
}

func TestArithmetic(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"(+ 1 2)", 3},
		{"(- 10 3)", 7},
		{"(* 4 5)", 20},
		{"(/ 20 4)", 5},
		{"(% 17 5)", 2},
		{"(+ 1 (+ 2 3))", 6},
		{"(* (+ 1 2) (- 5 2))", 9},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestComparison(t *testing.T) {
	tests := []struct {
		input    string
		expected bool
	}{
		{"(= 1 1)", true},
		{"(= 1 2)", false},
		{"(< 1 2)", true},
		{"(< 2 1)", false},
		{"(> 2 1)", true},
		{"(> 1 2)", false},
		{"(<= 1 1)", true},
		{"(<= 1 2)", true},
		{"(>= 2 2)", true},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil {
			t.Errorf("evalString(%q) = nil", tt.input)
			continue
		}
		isTrue := !ast.IsNil(result)
		if isTrue != tt.expected {
			t.Errorf("evalString(%q) = %v, want %v", tt.input, isTrue, tt.expected)
		}
	}
}

func TestLet(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"(let ((x 10)) x)", 10},
		{"(let ((x 5) (y 3)) (+ x y))", 8},
		{"(let ((x 10)) (let ((y 20)) (+ x y)))", 30},
		{"(let ((a 2) (b 3) (c 4)) (+ a (+ b c)))", 9},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestIf(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"(if t 1 2)", 1},
		{"(if () 1 2)", 2},
		{"(if (= 1 1) 100 200)", 100},
		{"(if (= 1 2) 100 200)", 200},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestLambda(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"(let ((f (lambda (x) (* x x)))) (f 5))", 25},
		{"(let ((add (lambda (a b) (+ a b)))) (add 3 4))", 7},
		{"(let ((f (lambda (x) (+ x 1)))) (f (f (f 0))))", 3},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestLetrec(t *testing.T) {
	// Factorial
	input := "(letrec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))) (fact 5))"
	result := evalString(input)
	if result == nil || !ast.IsInt(result) {
		t.Errorf("factorial = %v, want int", result)
		return
	}
	if result.Int != 120 {
		t.Errorf("factorial(5) = %d, want 120", result.Int)
	}
}

func TestLift(t *testing.T) {
	result := evalString("(lift 42)")
	if result == nil || !ast.IsCode(result) {
		t.Errorf("lift(42) = %v, want code", result)
		return
	}
	if result.Str != "mk_int(42)" {
		t.Errorf("lift(42) = %q, want mk_int(42)", result.Str)
	}
}

func TestCons(t *testing.T) {
	result := evalString("(cons 1 (cons 2 ()))")
	if result == nil || !ast.IsCell(result) {
		t.Errorf("cons result = %v, want cell", result)
		return
	}
	expected := "(1 2)"
	if result.String() != expected {
		t.Errorf("cons result = %q, want %q", result.String(), expected)
	}
}

func TestQuote(t *testing.T) {
	result := evalString("'foo")
	if result == nil || !ast.IsSym(result) {
		t.Errorf("quote = %v, want symbol", result)
		return
	}
	if result.Str != "foo" {
		t.Errorf("quote = %q, want foo", result.Str)
	}
}

func TestRecursiveLambda(t *testing.T) {
	// Factorial using (lambda self (n) ...)
	input := "(let ((fact (lambda self (n) (if (= n 0) 1 (* n (self (- n 1))))))) (fact 5))"
	result := evalString(input)
	if result == nil || !ast.IsInt(result) {
		t.Errorf("recursive lambda factorial = %v, want int", result)
		return
	}
	if result.Int != 120 {
		t.Errorf("recursive lambda factorial(5) = %d, want 120", result.Int)
	}
}

func TestMatch(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		// Wildcard
		{"(match 42 (_ 1))", 1},
		// Variable
		{"(match 42 (x x))", 42},
		// Literal
		{"(match 42 (42 100) (_ 0))", 100},
		{"(match 43 (42 100) (_ 0))", 0},
		// Cons pattern
		{"(match (cons 1 2) ((cons a b) (+ a b)))", 3},
		// Nested
		{"(match (cons 1 (cons 2 3)) ((cons a (cons b c)) (+ a (+ b c))))", 6},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestDo(t *testing.T) {
	// do should return the last expression
	result := evalString("(do 1 2 3)")
	if result == nil || !ast.IsInt(result) {
		t.Errorf("do = %v, want int", result)
		return
	}
	if result.Int != 3 {
		t.Errorf("do = %d, want 3", result.Int)
	}
}

func TestError(t *testing.T) {
	result := evalString("(error 'test-error)")
	if result == nil || !ast.IsError(result) {
		t.Errorf("error = %v, want error", result)
		return
	}
	if result.Str != "test-error" {
		t.Errorf("error message = %q, want test-error", result.Str)
	}
}

func TestTry(t *testing.T) {
	// try catches error
	result := evalString("(try (error 'oops) (lambda (e) 42))")
	if result == nil || !ast.IsInt(result) {
		t.Errorf("try = %v, want int", result)
		return
	}
	if result.Int != 42 {
		t.Errorf("try = %d, want 42", result.Int)
	}

	// try passes through non-error
	result = evalString("(try 100 (lambda (e) 42))")
	if result == nil || !ast.IsInt(result) {
		t.Errorf("try = %v, want int", result)
		return
	}
	if result.Int != 100 {
		t.Errorf("try = %d, want 100", result.Int)
	}
}

func TestListOperations(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"(length '(1 2 3))", 3},
		{"(length ())", 0},
		{"(car (append '(1 2) '(3 4)))", 1},
		{"(car (reverse '(1 2 3)))", 3},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestMap(t *testing.T) {
	result := evalString("(map (lambda (x) (* x 2)) '(1 2 3))")
	if result == nil || !ast.IsCell(result) {
		t.Errorf("map = %v, want list", result)
		return
	}
	// Should be (2 4 6)
	if result.Car.Int != 2 {
		t.Errorf("map first = %d, want 2", result.Car.Int)
	}
}

func TestFilter(t *testing.T) {
	result := evalString("(filter (lambda (x) (> x 2)) '(1 2 3 4 5))")
	if result == nil || !ast.IsCell(result) {
		t.Errorf("filter = %v, want list", result)
		return
	}
	// Should be (3 4 5)
	if result.Car.Int != 3 {
		t.Errorf("filter first = %d, want 3", result.Car.Int)
	}
}

func TestFold(t *testing.T) {
	// Sum using fold
	result := evalString("(fold + 0 '(1 2 3 4 5))")
	if result == nil || !ast.IsInt(result) {
		t.Errorf("fold = %v, want int", result)
		return
	}
	if result.Int != 15 {
		t.Errorf("fold sum = %d, want 15", result.Int)
	}
}

func TestQuasiquote(t *testing.T) {
	// Basic quasiquote
	result := evalString("`(a b c)")
	if result == nil || !ast.IsCell(result) {
		t.Errorf("quasiquote = %v, want list", result)
		return
	}
	if result.Car.Str != "a" {
		t.Errorf("quasiquote first = %s, want a", result.Car.Str)
	}

	// Unquote
	result = evalString("(let ((x 42)) `(a ,x c))")
	if result == nil || !ast.IsCell(result) {
		t.Errorf("unquote = %v, want list", result)
		return
	}
	// Second element should be 42
	second := result.Cdr.Car
	if !ast.IsInt(second) || second.Int != 42 {
		t.Errorf("unquote second = %v, want 42", second)
	}

	// Unquote-splicing
	result = evalString("(let ((xs '(1 2 3))) `(a ,@xs d))")
	if result == nil || !ast.IsCell(result) {
		t.Errorf("unquote-splicing = %v, want list", result)
		return
	}
	// Should be (a 1 2 3 d)
	if result.Car.Str != "a" {
		t.Errorf("splice first = %s, want a", result.Car.Str)
	}
}

func TestGensym(t *testing.T) {
	result := evalString("(gensym)")
	if result == nil || !ast.IsSym(result) {
		t.Errorf("gensym = %v, want symbol", result)
		return
	}
	// Should start with 'g'
	if len(result.Str) == 0 || result.Str[0] != 'g' {
		t.Errorf("gensym = %s, want g<n>", result.Str)
	}
}

func TestSymEq(t *testing.T) {
	result := evalString("(sym-eq? 'foo 'foo)")
	if ast.IsNil(result) {
		t.Error("sym-eq? 'foo 'foo should be true")
	}

	result = evalString("(sym-eq? 'foo 'bar)")
	if !ast.IsNil(result) {
		t.Error("sym-eq? 'foo 'bar should be false")
	}
}

func TestEvalExpr(t *testing.T) {
	// eval should evaluate quoted expressions
	result := evalString("(eval '(+ 1 2))")
	if result == nil || !ast.IsInt(result) {
		t.Errorf("eval = %v, want int", result)
		return
	}
	if result.Int != 3 {
		t.Errorf("eval (+ 1 2) = %d, want 3", result.Int)
	}
}

func TestString(t *testing.T) {
	// String literal parsing (returns list of chars)
	result := evalString("\"hello\"")
	if result == nil || !ast.IsCell(result) {
		t.Errorf("\"hello\" = %v, want list of chars", result)
		return
	}
	// First char should be 'h'
	if !ast.IsChar(result.Car) || result.Car.Int != 'h' {
		t.Errorf("first char = %v, want #\\h", result.Car)
	}

	// string? predicate
	result = evalString("(string? \"abc\")")
	if ast.IsNil(result) {
		t.Error("(string? \"abc\") should be true")
	}

	result = evalString("(string? '(1 2 3))")
	if !ast.IsNil(result) {
		t.Error("(string? '(1 2 3)) should be false")
	}

	// string-length
	result = evalString("(string-length \"hello\")")
	if result == nil || !ast.IsInt(result) || result.Int != 5 {
		t.Errorf("(string-length \"hello\") = %v, want 5", result)
	}

	// string-ref
	result = evalString("(string-ref \"hello\" 1)")
	if result == nil || !ast.IsChar(result) || result.Int != 'e' {
		t.Errorf("(string-ref \"hello\" 1) = %v, want #\\e", result)
	}

	// string-append
	result = evalString("(string-length (string-append \"ab\" \"cd\"))")
	if result == nil || !ast.IsInt(result) || result.Int != 4 {
		t.Errorf("string-append length = %v, want 4", result)
	}

	// list->string
	result = evalString("(list->string \"hi\")")
	if result == nil || !ast.IsSym(result) || result.Str != "hi" {
		t.Errorf("(list->string \"hi\") = %v, want symbol hi", result)
	}

	// substring
	result = evalString("(string-length (substring \"hello\" 1 3))")
	if result == nil || !ast.IsInt(result) || result.Int != 2 {
		t.Errorf("(substring \"hello\" 1 3) length = %v, want 2", result)
	}
}

func TestFloat(t *testing.T) {
	// Float literal parsing
	result := evalString("3.14")
	if result == nil || !ast.IsFloat(result) {
		t.Errorf("3.14 = %v, want float", result)
		return
	}
	if result.Float != 3.14 {
		t.Errorf("3.14 = %g, want 3.14", result.Float)
	}

	// Negative float
	result = evalString("-2.5")
	if result == nil || !ast.IsFloat(result) || result.Float != -2.5 {
		t.Errorf("-2.5 = %v, want -2.5", result)
	}

	// Scientific notation
	result = evalString("1e-3")
	if result == nil || !ast.IsFloat(result) || result.Float != 0.001 {
		t.Errorf("1e-3 = %v, want 0.001", result)
	}

	// Float arithmetic
	result = evalString("(+ 1.5 2.5)")
	if result == nil || !ast.IsFloat(result) || result.Float != 4.0 {
		t.Errorf("(+ 1.5 2.5) = %v, want 4.0", result)
	}

	// Mixed int/float arithmetic
	result = evalString("(* 2 3.5)")
	if result == nil || !ast.IsFloat(result) || result.Float != 7.0 {
		t.Errorf("(* 2 3.5) = %v, want 7.0", result)
	}

	// Float division
	result = evalString("(/ 7.0 2.0)")
	if result == nil || !ast.IsFloat(result) || result.Float != 3.5 {
		t.Errorf("(/ 7.0 2.0) = %v, want 3.5", result)
	}

	// float? predicate
	result = evalString("(float? 3.14)")
	if ast.IsNil(result) {
		t.Error("(float? 3.14) should be true")
	}

	result = evalString("(float? 42)")
	if !ast.IsNil(result) {
		t.Error("(float? 42) should be false")
	}

	// int->float
	result = evalString("(int->float 42)")
	if result == nil || !ast.IsFloat(result) || result.Float != 42.0 {
		t.Errorf("(int->float 42) = %v, want 42.0", result)
	}

	// float->int
	result = evalString("(float->int 3.7)")
	if result == nil || !ast.IsInt(result) || result.Int != 3 {
		t.Errorf("(float->int 3.7) = %v, want 3", result)
	}

	// abs
	result = evalString("(abs -3.5)")
	if result == nil || !ast.IsFloat(result) || result.Float != 3.5 {
		t.Errorf("(abs -3.5) = %v, want 3.5", result)
	}
}

func TestFFI(t *testing.T) {
	// FFI in codegen mode (with lifted values)
	result := evalString("(let ((x (lift 42))) (ffi 'printf x))")
	if result == nil || !ast.IsCode(result) {
		t.Errorf("ffi codegen = %v, want code", result)
		return
	}
	// The let generates a code block, check that printf(x) is in there
	if !strings.Contains(result.Str, "printf(x)") {
		t.Errorf("ffi codegen = %q, should contain printf(x)", result.Str)
	}

	// Direct FFI call with literal generates simpler code
	result = evalString("(ffi 'sin (lift 3.14))")
	if result == nil || !ast.IsCode(result) {
		t.Errorf("ffi sin = %v, want code", result)
		return
	}

	// FFI declaration
	ClearFFIDeclarations()
	evalString("(ffi-declare 'int 'custom_func 'int 'float)")
	decls := GetFFIDeclarations()
	if decls["custom_func"] == nil {
		t.Error("ffi-declare should register custom_func")
	} else {
		if decls["custom_func"].ReturnType != "int" {
			t.Errorf("return type = %s, want int", decls["custom_func"].ReturnType)
		}
	}

	// Generate declarations
	declStr := GenerateFFIDeclarations()
	if !strings.Contains(declStr, "extern int custom_func") {
		t.Errorf("generated decl = %q, should contain extern int custom_func", declStr)
	}
}

func TestChar(t *testing.T) {
	// Character literal parsing
	result := evalString("#\\a")
	if result == nil || !ast.IsChar(result) {
		t.Errorf("#\\a = %v, want char", result)
		return
	}
	if result.Int != 'a' {
		t.Errorf("#\\a = %d, want %d", result.Int, 'a')
	}

	// Named characters
	result = evalString("#\\newline")
	if result == nil || !ast.IsChar(result) || result.Int != '\n' {
		t.Errorf("#\\newline = %v, want newline char", result)
	}

	result = evalString("#\\space")
	if result == nil || !ast.IsChar(result) || result.Int != ' ' {
		t.Errorf("#\\space = %v, want space char", result)
	}

	// char? predicate
	result = evalString("(char? #\\x)")
	if ast.IsNil(result) {
		t.Error("(char? #\\x) should be true")
	}

	result = evalString("(char? 42)")
	if !ast.IsNil(result) {
		t.Error("(char? 42) should be false")
	}

	// char->int
	result = evalString("(char->int #\\A)")
	if result == nil || !ast.IsInt(result) || result.Int != 65 {
		t.Errorf("(char->int #\\A) = %v, want 65", result)
	}

	// int->char
	result = evalString("(int->char 66)")
	if result == nil || !ast.IsChar(result) || result.Int != 'B' {
		t.Errorf("(int->char 66) = %v, want #\\B", result)
	}

	// char=?
	result = evalString("(char=? #\\a #\\a)")
	if ast.IsNil(result) {
		t.Error("(char=? #\\a #\\a) should be true")
	}

	result = evalString("(char=? #\\a #\\b)")
	if !ast.IsNil(result) {
		t.Error("(char=? #\\a #\\b) should be false")
	}

	// char<?
	result = evalString("(char<? #\\a #\\b)")
	if ast.IsNil(result) {
		t.Error("(char<? #\\a #\\b) should be true")
	}
}
