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

// Tests for Tower of Interpreters features

func TestMetaLevel(t *testing.T) {
	// Base level should be 0
	result := evalString("(meta-level)")
	if result == nil || !ast.IsInt(result) || result.Int != 0 {
		t.Errorf("(meta-level) = %v, want 0", result)
	}
}

func TestShift(t *testing.T) {
	// shift 0 should evaluate at current level
	result := evalString("(shift 0 (+ 1 2))")
	if result == nil || !ast.IsInt(result) || result.Int != 3 {
		t.Errorf("(shift 0 (+ 1 2)) = %v, want 3", result)
	}

	// shift 1 should evaluate at parent level (level 1)
	result = evalString("(shift 1 (meta-level))")
	if result == nil || !ast.IsInt(result) || result.Int != 1 {
		t.Errorf("(shift 1 (meta-level)) = %v, want 1", result)
	}

	// shift 2 should evaluate at grandparent level (level 2)
	result = evalString("(shift 2 (meta-level))")
	if result == nil || !ast.IsInt(result) || result.Int != 2 {
		t.Errorf("(shift 2 (meta-level)) = %v, want 2", result)
	}
}

func TestEM(t *testing.T) {
	// EM should escape to parent level
	result := evalString("(EM (meta-level))")
	if result == nil || !ast.IsInt(result) || result.Int != 1 {
		t.Errorf("(EM (meta-level)) = %v, want 1", result)
	}

	// Nested EM should go up multiple levels
	result = evalString("(EM (EM (meta-level)))")
	if result == nil || !ast.IsInt(result) || result.Int != 2 {
		t.Errorf("(EM (EM (meta-level))) = %v, want 2", result)
	}
}

func TestGetMeta(t *testing.T) {
	// get-meta should return a marker for native handlers
	result := evalString("(get-meta 'lit)")
	if result == nil {
		t.Error("(get-meta 'lit) should not be nil")
	}
	// Should return #<native-handler:lit> or similar
	if ast.IsSym(result) && !strings.Contains(result.Str, "native-handler") {
		t.Errorf("(get-meta 'lit) = %v, want native handler marker", result)
	}
}

func TestWithHandlers(t *testing.T) {
	// Custom lit handler that adds 100 to all literals
	result := evalString(`
		(with-handlers
			((lit (lambda (x) (+ 100 (default-handler 'lit x)))))
			(+ 10 20))
	`)
	// 10 becomes 110, 20 becomes 120, so 110 + 120 = 230
	if result == nil || !ast.IsInt(result) || result.Int != 230 {
		t.Errorf("with-handlers custom lit = %v, want 230", result)
	}
}

func TestWithHandlersScoped(t *testing.T) {
	// Handler changes should be scoped
	result := evalString(`
		(let ((a (+ 1 2)))          ; a = 3 (normal)
			(let ((b (with-handlers
						((lit (lambda (x) (+ 100 (default-handler 'lit x)))))
						(+ 1 2))))  ; b = 203 (custom: 101 + 102)
				(+ a b)))           ; 3 + 203 = 206
	`)
	if result == nil || !ast.IsInt(result) || result.Int != 206 {
		t.Errorf("scoped handlers = %v, want 206", result)
	}
}

func TestSetMeta(t *testing.T) {
	// set-meta! returns new menv, used with with-menv
	result := evalString(`
		(with-menv
			(set-meta! 'lit (lambda (x) (+ 50 (default-handler 'lit x))))
			(+ 10 20))
	`)
	// 10 becomes 60, 20 becomes 70, so 60 + 70 = 130
	if result == nil || !ast.IsInt(result) || result.Int != 130 {
		t.Errorf("set-meta! custom lit = %v, want 130", result)
	}
}

func TestDefaultHandler(t *testing.T) {
	// default-handler should call the default behavior
	result := evalString("(default-handler 'lit 42)")
	if result == nil || !ast.IsInt(result) || result.Int != 42 {
		t.Errorf("(default-handler 'lit 42) = %v, want 42", result)
	}
}

func TestHandlerDelegation(t *testing.T) {
	// Test handler that wraps default behavior
	// This is the classic "tower" pattern
	result := evalString(`
		(with-handlers
			((lit (lambda (x)
				(let ((base (default-handler 'lit x)))
					(* base 2)))))
			(+ 3 4))
	`)
	// 3 becomes 6, 4 becomes 8, so 6 + 8 = 14
	if result == nil || !ast.IsInt(result) || result.Int != 14 {
		t.Errorf("handler delegation = %v, want 14", result)
	}
}

// =============================================================================
// Macro System Tests
// =============================================================================

func TestDefmacroSimple(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Define a simple macro that doubles its argument
	result := evalString(`
		(defmacro double (x)
			(quasiquote (+ (unquote x) (unquote x)))
			(mcall double 5))
	`)
	// (double 5) expands to (+ 5 5) = 10
	if result == nil || !ast.IsInt(result) || result.Int != 10 {
		t.Errorf("defmacro double = %v, want 10", result)
	}
}

func TestDefmacroWithScope(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Define macro and use it in scope
	result := evalString(`
		(defmacro inc (x)
			(quasiquote (+ 1 (unquote x)))
			(+ (mcall inc 5) (mcall inc 10)))
	`)
	// (inc 5) = (+ 1 5) = 6, (inc 10) = (+ 1 10) = 11, total = 17
	if result == nil || !ast.IsInt(result) || result.Int != 17 {
		t.Errorf("defmacro with scope = %v, want 17", result)
	}
}

func TestMcall(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Define a macro, then call it with mcall
	result := evalString(`
		(defmacro square (x)
			(quasiquote (* (unquote x) (unquote x)))
			(mcall square 4))
	`)
	// (square 4) expands to (* 4 4) = 16
	if result == nil || !ast.IsInt(result) || result.Int != 16 {
		t.Errorf("mcall square = %v, want 16", result)
	}
}

func TestMcallUndefinedMacro(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Try to call undefined macro
	result := evalString(`(mcall undefined-macro 1 2 3)`)
	if result == nil || !ast.IsError(result) {
		t.Errorf("mcall undefined = %v, want error", result)
	}
}

func TestMacroexpand(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Define macro, then expand without evaluating
	result := evalString(`
		(defmacro negate (x)
			(quasiquote (- 0 (unquote x)))
			(macroexpand (mcall negate 42)))
	`)
	// Should expand to (- 0 42), not evaluate to -42
	// The result should be a list: (- 0 42)
	if result == nil || !ast.IsCell(result) {
		t.Errorf("macroexpand = %v, want list", result)
		return
	}

	// Check structure: (- 0 42)
	op := result.Car
	if !ast.IsSym(op) || op.Str != "-" {
		t.Errorf("macroexpand op = %v, want -", op)
	}
}

func TestMacroMultipleParams(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Macro with multiple parameters
	result := evalString(`
		(defmacro add3 (a b c)
			(quasiquote (+ (unquote a) (+ (unquote b) (unquote c))))
			(mcall add3 1 2 3))
	`)
	// (add3 1 2 3) = (+ 1 (+ 2 3)) = 6
	if result == nil || !ast.IsInt(result) || result.Int != 6 {
		t.Errorf("macro multi-params = %v, want 6", result)
	}
}

func TestMacroWithQuote(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Macro that quotes its argument
	result := evalString(`
		(defmacro quote-it (x)
			(quasiquote (quote (unquote x)))
			(mcall quote-it hello))
	`)
	// Should return the symbol 'hello
	if result == nil || !ast.IsSym(result) || result.Str != "hello" {
		t.Errorf("quote-it = %v, want symbol hello", result)
	}
}

func TestMacroNestedCalls(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Nested macro calls
	result := evalString(`
		(defmacro double (x)
			(quasiquote (+ (unquote x) (unquote x)))
			(mcall double (mcall double 3)))
	`)
	// inner: (double 3) = (+ 3 3) = 6
	// outer: (double 6) = (+ 6 6) = 12
	if result == nil || !ast.IsInt(result) || result.Int != 12 {
		t.Errorf("nested macro calls = %v, want 12", result)
	}
}

func TestMacroWithConditional(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Macro that generates conditional code
	result := evalString(`
		(defmacro when (cond body)
			(quasiquote (if (unquote cond) (unquote body) nil))
			(mcall when (> 5 3) 42))
	`)
	// (when (> 5 3) 42) expands to (if (> 5 3) 42 nil), which evaluates to 42
	if result == nil || !ast.IsInt(result) || result.Int != 42 {
		t.Errorf("macro when = %v, want 42", result)
	}
}

func TestMacroWithLetBinding(t *testing.T) {
	// Clear macros before test
	ClearMacros()

	// Macro that generates let binding
	result := evalString(`
		(defmacro with-x (val body)
			(quasiquote (let ((x (unquote val))) (unquote body)))
			(mcall with-x 10 (+ x 5)))
	`)
	// (with-x 10 (+ x 5)) = (let ((x 10)) (+ x 5)) = 15
	if result == nil || !ast.IsInt(result) || result.Int != 15 {
		t.Errorf("macro with-x = %v, want 15", result)
	}
}

// =============================================================================
// Mutable State Tests (Phase 1)
// =============================================================================

func TestBox(t *testing.T) {
	// Create a box
	result := evalString("(box 42)")
	if result == nil || !ast.IsBox(result) {
		t.Errorf("(box 42) = %v, want box", result)
		return
	}
	if result.BoxValue.Int != 42 {
		t.Errorf("(box 42) value = %d, want 42", result.BoxValue.Int)
	}

	// box? predicate
	result = evalString("(box? (box 1))")
	if ast.IsNil(result) {
		t.Error("(box? (box 1)) should be true")
	}

	result = evalString("(box? 42)")
	if !ast.IsNil(result) {
		t.Error("(box? 42) should be false")
	}
}

func TestUnbox(t *testing.T) {
	// Unbox a value
	result := evalString("(unbox (box 100))")
	if result == nil || !ast.IsInt(result) || result.Int != 100 {
		t.Errorf("(unbox (box 100)) = %v, want 100", result)
	}

	// Unbox a list
	result = evalString("(car (unbox (box '(1 2 3))))")
	if result == nil || !ast.IsInt(result) || result.Int != 1 {
		t.Errorf("(car (unbox (box '(1 2 3)))) = %v, want 1", result)
	}
}

func TestSetBox(t *testing.T) {
	// Create box, set its value, unbox
	result := evalString(`
		(let ((b (box 10)))
			(do
				(set-box! b 20)
				(unbox b)))
	`)
	if result == nil || !ast.IsInt(result) || result.Int != 20 {
		t.Errorf("set-box! = %v, want 20", result)
	}

	// Multiple updates
	result = evalString(`
		(let ((counter (box 0)))
			(do
				(set-box! counter (+ 1 (unbox counter)))
				(set-box! counter (+ 1 (unbox counter)))
				(set-box! counter (+ 1 (unbox counter)))
				(unbox counter)))
	`)
	if result == nil || !ast.IsInt(result) || result.Int != 3 {
		t.Errorf("set-box! counter = %v, want 3", result)
	}
}

func TestSetBang(t *testing.T) {
	// set! mutates existing binding
	result := evalString(`
		(let ((x 10))
			(do
				(set! x 20)
				x))
	`)
	if result == nil || !ast.IsInt(result) || result.Int != 20 {
		t.Errorf("set! = %v, want 20", result)
	}

	// Nested let with set!
	result = evalString(`
		(let ((x 1))
			(let ((y 2))
				(do
					(set! x 10)
					(set! y 20)
					(+ x y))))
	`)
	if result == nil || !ast.IsInt(result) || result.Int != 30 {
		t.Errorf("nested set! = %v, want 30", result)
	}

	// set! in loop-like construct
	result = evalString(`
		(let ((sum 0))
			(let ((i 1))
				(letrec ((loop (lambda ()
					(if (> i 5)
						sum
						(do
							(set! sum (+ sum i))
							(set! i (+ i 1))
							(loop))))))
					(loop))))
	`)
	// sum = 1 + 2 + 3 + 4 + 5 = 15
	if result == nil || !ast.IsInt(result) || result.Int != 15 {
		t.Errorf("set! loop = %v, want 15", result)
	}
}

func TestDefine(t *testing.T) {
	// Reset global env for test isolation
	ResetGlobalEnv()
	InitGlobalEnv()

	// Simple define
	evalString("(define answer 42)")
	result := evalString("answer")
	if result == nil || !ast.IsInt(result) || result.Int != 42 {
		t.Errorf("(define answer 42) = %v, want 42", result)
	}

	// Define function shorthand
	evalString("(define (square x) (* x x))")
	result = evalString("(square 7)")
	if result == nil || !ast.IsInt(result) || result.Int != 49 {
		t.Errorf("(square 7) = %v, want 49", result)
	}

	// Define recursive function
	evalString("(define (factorial n) (if (= n 0) 1 (* n (factorial (- n 1)))))")
	result = evalString("(factorial 5)")
	if result == nil || !ast.IsInt(result) || result.Int != 120 {
		t.Errorf("(factorial 5) = %v, want 120", result)
	}

	// Redefine
	evalString("(define answer 100)")
	result = evalString("answer")
	if result == nil || !ast.IsInt(result) || result.Int != 100 {
		t.Errorf("redefined answer = %v, want 100", result)
	}
}

func TestDefineMultipleArgs(t *testing.T) {
	// Reset global env for test isolation
	ResetGlobalEnv()
	InitGlobalEnv()

	// Define with multiple arguments
	evalString("(define (add3 a b c) (+ a (+ b c)))")
	result := evalString("(add3 1 2 3)")
	if result == nil || !ast.IsInt(result) || result.Int != 6 {
		t.Errorf("(add3 1 2 3) = %v, want 6", result)
	}

	// Define with no arguments
	evalString("(define (get-pi) 314)")
	result = evalString("(get-pi)")
	if result == nil || !ast.IsInt(result) || result.Int != 314 {
		t.Errorf("(get-pi) = %v, want 314", result)
	}
}

func TestSetBangUnbound(t *testing.T) {
	// set! on unbound variable should error
	result := evalString("(set! unbound-var 42)")
	if result == nil || !ast.IsError(result) {
		t.Errorf("set! unbound = %v, want error", result)
	}
}

func TestMutableCounter(t *testing.T) {
	// Classic mutable counter example using box
	result := evalString(`
		(let ((make-counter (lambda ()
				(let ((count (box 0)))
					(lambda ()
						(do
							(set-box! count (+ 1 (unbox count)))
							(unbox count)))))))
			(let ((counter (make-counter)))
				(do
					(counter)
					(counter)
					(counter))))
	`)
	if result == nil || !ast.IsInt(result) || result.Int != 3 {
		t.Errorf("mutable counter = %v, want 3", result)
	}
}

func TestDeftype(t *testing.T) {
	// deftype defines a type with fields
	result := evalString("(deftype Node (value int) (next Node))")
	if result == nil || !ast.IsSym(result) {
		t.Errorf("deftype = %v, want symbol Node", result)
		return
	}
	if result.Str != "Node" {
		t.Errorf("deftype = %s, want Node", result.Str)
	}
}

func TestCtrArgUserType(t *testing.T) {
	// Define a type with multiple fields
	evalString("(deftype Point (x int) (y int) (z int))")

	// Create a Point instance and test ctr-arg returns fields in definition order
	result := evalString("(ctr-arg (mk-Point 10 20 30) 0)")
	if result == nil || !ast.IsInt(result) || result.Int != 10 {
		t.Errorf("(ctr-arg point 0) = %v, want 10", result)
	}

	result = evalString("(ctr-arg (mk-Point 10 20 30) 1)")
	if result == nil || !ast.IsInt(result) || result.Int != 20 {
		t.Errorf("(ctr-arg point 1) = %v, want 20", result)
	}

	result = evalString("(ctr-arg (mk-Point 10 20 30) 2)")
	if result == nil || !ast.IsInt(result) || result.Int != 30 {
		t.Errorf("(ctr-arg point 2) = %v, want 30", result)
	}

	// Test out of bounds index
	result = evalString("(ctr-arg (mk-Point 10 20 30) 3)")
	if result == nil || !ast.IsError(result) {
		t.Errorf("(ctr-arg point 3) = %v, want error", result)
	}
}

func TestUserTypeString(t *testing.T) {
	// Define a simple type
	evalString("(deftype Person (name int) (age int))")

	// Create a Person instance and check its String representation
	result := evalString("(mk-Person 42 25)")
	if result == nil || !ast.IsUserType(result) {
		t.Errorf("mk-Person = %v, want UserType", result)
		return
	}

	// Verify String() output format
	str := result.String()
	if !strings.Contains(str, "Person") {
		t.Errorf("UserType.String() = %q, should contain 'Person'", str)
	}
	if !strings.Contains(str, "name=42") {
		t.Errorf("UserType.String() = %q, should contain 'name=42'", str)
	}
	if !strings.Contains(str, "age=25") {
		t.Errorf("UserType.String() = %q, should contain 'age=25'", str)
	}

	// Verify TagName works for TUserType
	tagName := ast.TagName(ast.TUserType)
	if tagName != "USERTYPE" {
		t.Errorf("TagName(TUserType) = %q, want 'USERTYPE'", tagName)
	}
}

// =============================================================================
// Continuation Tests (Phase 4)
// =============================================================================

func TestCallCC(t *testing.T) {
	// Basic call/cc - return normally
	result := evalString("(call/cc (lambda (k) 42))")
	if result == nil || !ast.IsInt(result) || result.Int != 42 {
		t.Errorf("call/cc normal = %v, want 42", result)
	}

	// call/cc with early return
	result = evalString("(+ 1 (call/cc (lambda (k) (k 10) 20)))")
	// (k 10) escapes, so the result is (+ 1 10) = 11
	if result == nil || !ast.IsInt(result) || result.Int != 11 {
		t.Errorf("call/cc early return = %v, want 11", result)
	}

	// call/cc for early exit from loop
	result = evalString(`
		(call/cc (lambda (return)
			(letrec ((loop (lambda (n)
				(if (= n 5)
					(return 100)
					(loop (+ n 1))))))
				(loop 0))))
	`)
	if result == nil || !ast.IsInt(result) || result.Int != 100 {
		t.Errorf("call/cc loop exit = %v, want 100", result)
	}

	// Nested call/cc
	result = evalString(`
		(+ (call/cc (lambda (k1)
				(+ 1 (call/cc (lambda (k2) (k1 10))))))
			5)
	`)
	// Inner k1 call escapes with 10, result is (+ 10 5) = 15
	if result == nil || !ast.IsInt(result) || result.Int != 15 {
		t.Errorf("nested call/cc = %v, want 15", result)
	}
}

func TestCallCCException(t *testing.T) {
	// Use call/cc for exception-like behavior
	result := evalString(`
		(call/cc (lambda (throw)
			(let ((x 10))
				(if (> x 5)
					(throw 'too-big)
					x))))
	`)
	if result == nil || !ast.IsSym(result) || result.Str != "too-big" {
		t.Errorf("call/cc exception = %v, want too-big", result)
	}
}

func TestPrompt(t *testing.T) {
	// Prompt without control just evaluates body
	result := evalString("(prompt (+ 1 2))")
	if result == nil || !ast.IsInt(result) || result.Int != 3 {
		t.Errorf("prompt simple = %v, want 3", result)
	}
}

func TestControlPrompt(t *testing.T) {
	// Control immediately returns a value (aborts to prompt)
	result := evalString("(prompt (control k 42))")
	if result == nil || !ast.IsInt(result) || result.Int != 42 {
		t.Errorf("control/prompt simple = %v, want 42", result)
	}

	// Control with continuation call (simple form - k just returns value)
	result = evalString("(prompt (control k (k 10)))")
	// In our simplified implementation, (k 10) just returns 10
	if result == nil || !ast.IsInt(result) || result.Int != 10 {
		t.Errorf("control/prompt with k = %v, want 10", result)
	}
}

// =============================================================================
// CSP Concurrency Tests (Phase 5)
// =============================================================================

func TestMakeChan(t *testing.T) {
	// Create unbuffered channel
	result := evalString("(make-chan)")
	if result == nil || !ast.IsChan(result) {
		t.Errorf("(make-chan) = %v, want channel", result)
	}

	// Create buffered channel
	result = evalString("(make-chan 5)")
	if result == nil || !ast.IsChan(result) {
		t.Errorf("(make-chan 5) = %v, want channel", result)
	}
	if result.ChanCap != 5 {
		t.Errorf("(make-chan 5) capacity = %d, want 5", result.ChanCap)
	}

	// chan? predicate
	result = evalString("(chan? (make-chan))")
	if ast.IsNil(result) {
		t.Error("(chan? (make-chan)) should be true")
	}

	result = evalString("(chan? 42)")
	if !ast.IsNil(result) {
		t.Error("(chan? 42) should be false")
	}
}

func TestChannelOps(t *testing.T) {
	// Test buffered channel send/recv
	result := evalString(`
		(let ((ch (make-chan 1)))
			(do
				(chan-send! ch 42)
				(chan-recv! ch)))
	`)
	if result == nil || !ast.IsInt(result) || result.Int != 42 {
		t.Errorf("chan send/recv = %v, want 42", result)
	}
}

func TestGo(t *testing.T) {
	// Spawn a goroutine
	result := evalString("(go (+ 1 2))")
	if result == nil || !ast.IsProcess(result) {
		t.Errorf("(go ...) = %v, want process", result)
	}

	// process? predicate
	result = evalString("(process? (go 1))")
	if ast.IsNil(result) {
		t.Error("(process? (go 1)) should be true")
	}
}

func TestProducerConsumer(t *testing.T) {
	// Simple producer-consumer with buffered channel
	result := evalString(`
		(let ((ch (make-chan 10)))
			(do
				(chan-send! ch 1)
				(chan-send! ch 2)
				(chan-send! ch 3)
				(+ (chan-recv! ch) (+ (chan-recv! ch) (chan-recv! ch)))))
	`)
	// 1 + 2 + 3 = 6
	if result == nil || !ast.IsInt(result) || result.Int != 6 {
		t.Errorf("producer-consumer = %v, want 6", result)
	}
}

func TestCtrTag(t *testing.T) {
	// ctr-tag returns the constructor name as a symbol
	result := evalString("(ctr-tag 42)")
	if result == nil || !ast.IsSym(result) || result.Str != "int" {
		t.Errorf("(ctr-tag 42) = %v, want 'int", result)
	}

	result = evalString("(ctr-tag (cons 1 2))")
	if result == nil || !ast.IsSym(result) || result.Str != "cell" {
		t.Errorf("(ctr-tag (cons 1 2)) = %v, want 'cell", result)
	}

	result = evalString("(ctr-tag nil)")
	if result == nil || !ast.IsSym(result) || result.Str != "nil" {
		t.Errorf("(ctr-tag nil) = %v, want 'nil", result)
	}

	result = evalString("(ctr-tag (box 42))")
	if result == nil || !ast.IsSym(result) || result.Str != "box" {
		t.Errorf("(ctr-tag (box 42)) = %v, want 'box", result)
	}
}

func TestCtrArg(t *testing.T) {
	// ctr-arg returns the nth constructor argument
	result := evalString("(ctr-arg (cons 1 2) 0)")
	if result == nil || !ast.IsInt(result) || result.Int != 1 {
		t.Errorf("(ctr-arg (cons 1 2) 0) = %v, want 1", result)
	}

	result = evalString("(ctr-arg (cons 1 2) 1)")
	if result == nil || !ast.IsInt(result) || result.Int != 2 {
		t.Errorf("(ctr-arg (cons 1 2) 1) = %v, want 2", result)
	}

	result = evalString("(ctr-arg (box 99) 0)")
	if result == nil || !ast.IsInt(result) || result.Int != 99 {
		t.Errorf("(ctr-arg (box 99) 0) = %v, want 99", result)
	}
}

func TestReifyEnv(t *testing.T) {
	// reify-env returns the environment as an assoc list
	result := evalString("(let ((x 1)) (reify-env))")
	if result == nil || ast.IsNil(result) || !ast.IsCell(result) {
		t.Errorf("(reify-env) = %v, want non-empty list", result)
	}
}

func TestSelectRecvBinding(t *testing.T) {
	// Test that select recv can bind the received value to a variable
	// (recv ch var => body) should bind received value to var
	result := evalString(`
		(let ((ch (make-chan 1)))
			(do
				(chan-send! ch 42)
				(select
					(recv ch x => (+ x 10)))))
	`)
	// Should receive 42 and bind to x, then evaluate (+ x 10) = 52
	if result == nil || !ast.IsInt(result) || result.Int != 52 {
		t.Errorf("select recv with binding = %v, want 52", result)
	}

	// Test recv without variable binding still works
	result = evalString(`
		(let ((ch (make-chan 1)))
			(do
				(chan-send! ch 99)
				(select
					(recv ch => 100))))
	`)
	// Should receive and return 100 (body doesn't use received value)
	if result == nil || !ast.IsInt(result) || result.Int != 100 {
		t.Errorf("select recv without binding = %v, want 100", result)
	}
}
