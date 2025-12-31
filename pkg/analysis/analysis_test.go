package analysis

import (
	"testing"

	"purple_go/pkg/ast"
	"purple_go/pkg/parser"
)

func parseExpr(input string) *ast.Value {
	p := parser.New(input)
	expr, _ := p.Parse()
	return expr
}

func TestEscapeAnalysis(t *testing.T) {
	ctx := NewAnalysisContext()
	ctx.AddVar("x")
	ctx.AddVar("y")

	expr := parseExpr("(let ((z (+ x y))) z)")
	ctx.AnalyzeExpr(expr)
	ctx.AnalyzeEscape(expr, EscapeGlobal)

	xUsage := ctx.FindVar("x")
	if xUsage == nil {
		t.Fatal("x not found")
	}
	if xUsage.UseCount != 1 {
		t.Errorf("x.UseCount = %d, want 1", xUsage.UseCount)
	}

	yUsage := ctx.FindVar("y")
	if yUsage == nil {
		t.Fatal("y not found")
	}
	if yUsage.UseCount != 1 {
		t.Errorf("y.UseCount = %d, want 1", yUsage.UseCount)
	}
}

func TestLambdaCapture(t *testing.T) {
	ctx := NewAnalysisContext()
	ctx.AddVar("x")

	expr := parseExpr("(lambda (y) (+ x y))")
	ctx.AnalyzeExpr(expr)

	xUsage := ctx.FindVar("x")
	if xUsage == nil {
		t.Fatal("x not found")
	}
	if !xUsage.CapturedByLambda {
		t.Error("x should be captured by lambda")
	}
}

func TestShapeAnalysisTree(t *testing.T) {
	ctx := NewShapeContext()

	// Literal integers are TREE
	expr := parseExpr("42")
	ctx.AnalyzeShapes(expr)

	if ctx.ResultShape != ShapeTree {
		t.Errorf("integer shape = %s, want TREE", ShapeString(ctx.ResultShape))
	}
}

func TestShapeAnalysisCons(t *testing.T) {
	ctx := NewShapeContext()

	// cons of two trees is a tree (if not aliased)
	ctx.AddShape("a", ShapeTree)
	ctx.AddShape("b", ShapeTree)

	expr := parseExpr("(cons a b)")
	ctx.AnalyzeShapes(expr)

	// Since a and b might alias (conservative), result is DAG
	if ctx.ResultShape != ShapeDAG && ctx.ResultShape != ShapeTree {
		t.Errorf("cons shape = %s, want TREE or DAG", ShapeString(ctx.ResultShape))
	}
}

func TestShapeAnalysisLetrec(t *testing.T) {
	ctx := NewShapeContext()

	// letrec creates potentially cyclic structures
	expr := parseExpr("(letrec ((x (cons 1 x))) x)")
	ctx.AnalyzeShapes(expr)

	xInfo := ctx.FindShape("x")
	if xInfo == nil {
		t.Fatal("x shape info not found")
	}
	if xInfo.Shape != ShapeCyclic {
		t.Errorf("letrec x shape = %s, want CYCLIC", ShapeString(xInfo.Shape))
	}
}

func TestShapeJoin(t *testing.T) {
	tests := []struct {
		a, b     Shape
		expected Shape
	}{
		{ShapeTree, ShapeTree, ShapeTree},
		{ShapeTree, ShapeDAG, ShapeDAG},
		{ShapeDAG, ShapeDAG, ShapeDAG},
		{ShapeTree, ShapeCyclic, ShapeCyclic},
		{ShapeDAG, ShapeCyclic, ShapeCyclic},
		{ShapeCyclic, ShapeCyclic, ShapeCyclic},
	}

	for _, tt := range tests {
		result := ShapeJoin(tt.a, tt.b)
		if result != tt.expected {
			t.Errorf("ShapeJoin(%s, %s) = %s, want %s",
				ShapeString(tt.a), ShapeString(tt.b),
				ShapeString(result), ShapeString(tt.expected))
		}
	}
}

func TestShapeFreeStrategy(t *testing.T) {
	tests := []struct {
		shape    Shape
		expected string
	}{
		{ShapeTree, "free_tree"},
		{ShapeDAG, "dec_ref"},
		{ShapeCyclic, "deferred_release"},
		{ShapeUnknown, "dec_ref"},
	}

	for _, tt := range tests {
		result := ShapeFreeStrategy(tt.shape)
		if result != tt.expected {
			t.Errorf("ShapeFreeStrategy(%s) = %s, want %s",
				ShapeString(tt.shape), result, tt.expected)
		}
	}
}

func TestFindFreeVars(t *testing.T) {
	expr := parseExpr("(lambda (x) (+ x y z))")
	bound := map[string]bool{"y": true}
	freeVars := FindFreeVars(expr, bound)

	// z should be free (not bound, not a parameter)
	found := false
	for _, v := range freeVars {
		if v == "z" {
			found = true
			break
		}
	}
	if !found {
		t.Errorf("z should be in free vars, got %v", freeVars)
	}

	// y should not be free (it's bound)
	for _, v := range freeVars {
		if v == "y" {
			t.Errorf("y should not be in free vars (it's bound)")
		}
	}
}
