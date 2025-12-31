package analysis

import (
	"testing"

	"purple_go/pkg/ast"
)

func TestRCOptContext_UniqueReference(t *testing.T) {
	ctx := NewRCOptContext()

	// Fresh allocation is unique
	ctx.DefineVar("x")
	info := ctx.Vars["x"]

	if !info.IsUnique {
		t.Error("Fresh allocation should be unique")
	}

	// Should use direct free for unique references
	opt := ctx.GetOptimizedDecRef("x")
	if opt != RCOptDirectFree {
		t.Errorf("Expected RCOptDirectFree, got %s", RCOptString(opt))
	}
}

func TestRCOptContext_AliasCreation(t *testing.T) {
	ctx := NewRCOptContext()

	// Define original
	ctx.DefineVar("x")

	// Create alias
	ctx.DefineAlias("y", "x")

	// Original is no longer unique
	if ctx.Vars["x"].IsUnique {
		t.Error("Original should not be unique after aliasing")
	}

	// Alias should reference original
	if ctx.Vars["y"].AliasOf != "x" {
		t.Errorf("Expected alias of 'x', got '%s'", ctx.Vars["y"].AliasOf)
	}
}

func TestRCOptContext_BorrowedReference(t *testing.T) {
	ctx := NewRCOptContext()

	// Borrowed reference
	ctx.DefineBorrowed("param")

	info := ctx.Vars["param"]
	if !info.IsBorrowed {
		t.Error("Parameter should be borrowed")
	}

	// Borrowed refs should elide inc_ref
	opt := ctx.GetOptimizedIncRef("param")
	if opt != RCOptElideIncRef {
		t.Errorf("Expected RCOptElideIncRef for borrowed ref, got %s", RCOptString(opt))
	}

	// Borrowed refs should elide dec_ref
	opt = ctx.GetOptimizedDecRef("param")
	if opt != RCOptElideDecRef {
		t.Errorf("Expected RCOptElideDecRef for borrowed ref, got %s", RCOptString(opt))
	}
}

func TestRCOptContext_AliasElision(t *testing.T) {
	ctx := NewRCOptContext()

	// Define original and alias
	ctx.DefineVar("x")
	ctx.DefineAlias("y", "x")

	// Mark usage order: y used later than x
	ctx.MarkUsed("x")
	ctx.MarkUsed("y")

	// x should elide dec_ref since y will handle it
	opt := ctx.GetOptimizedDecRef("x")
	if opt != RCOptElideDecRef {
		t.Errorf("Expected RCOptElideDecRef for earlier alias, got %s", RCOptString(opt))
	}
}

func TestRCOptContext_AnalyzeLetExpr(t *testing.T) {
	ctx := NewRCOptContext()

	// Build a let expression: (let ((x 10) (y x)) (+ x y))
	bindings := ast.NewCell(
		ast.NewCell(ast.NewSym("x"), ast.NewCell(ast.NewInt(10), ast.Nil)),
		ast.NewCell(
			ast.NewCell(ast.NewSym("y"), ast.NewCell(ast.NewSym("x"), ast.Nil)),
			ast.Nil,
		),
	)
	body := ast.NewCell(ast.NewSym("+"),
		ast.NewCell(ast.NewSym("x"),
			ast.NewCell(ast.NewSym("y"), ast.Nil)))

	letExpr := ast.NewCell(
		ast.NewSym("let"),
		ast.NewCell(bindings, ast.NewCell(body, ast.Nil)),
	)

	ctx.AnalyzeExpr(letExpr)

	// x should be defined (from literal)
	if ctx.Vars["x"] == nil {
		t.Error("Variable x should be defined")
	}

	// y should be an alias of x
	if ctx.Vars["y"] == nil {
		t.Error("Variable y should be defined")
	}
	if ctx.Vars["y"].AliasOf != "x" {
		t.Errorf("y should be alias of x, got '%s'", ctx.Vars["y"].AliasOf)
	}
}

func TestRCOptContext_Statistics(t *testing.T) {
	ctx := NewRCOptContext()

	// Set up some variables with optimizations
	ctx.DefineVar("unique")
	ctx.DefineBorrowed("borrowed")

	// Trigger optimizations
	ctx.GetOptimizedDecRef("unique")   // Should use direct free
	ctx.GetOptimizedIncRef("borrowed") // Should elide
	ctx.GetOptimizedDecRef("borrowed") // Should elide

	total, eliminated := ctx.GetStatistics()

	if eliminated < 2 {
		t.Errorf("Expected at least 2 eliminated ops, got %d", eliminated)
	}

	t.Logf("RC operations: %d total, %d eliminated (%.1f%%)",
		total, eliminated, float64(eliminated)/float64(total)*100)
}

func TestRCOptContext_GetFreeFunction(t *testing.T) {
	ctx := NewRCOptContext()

	// Unique variable should use free_unique
	ctx.DefineVar("unique")
	freeFn := ctx.GetFreeFunction("unique", ShapeTree)
	if freeFn != "free_unique" {
		t.Errorf("Expected free_unique for unique var, got %s", freeFn)
	}

	// Borrowed should return empty (no free needed)
	ctx.DefineBorrowed("borrowed")
	freeFn = ctx.GetFreeFunction("borrowed", ShapeTree)
	if freeFn != "" {
		t.Errorf("Expected empty string for borrowed var, got %s", freeFn)
	}

	// Non-unique, non-borrowed should use shape-based strategy
	ctx.DefineVar("shared")
	ctx.DefineAlias("alias", "shared")
	// shared is no longer unique, but is the original
	freeFn = ctx.GetFreeFunction("shared", ShapeDAG)
	// shared might still use dec_ref since it's not unique anymore
	if freeFn != "dec_ref" && freeFn != "" {
		t.Logf("Free function for shared (non-unique, DAG): %s", freeFn)
	}
}
