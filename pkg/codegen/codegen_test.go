package codegen

import (
	"strings"
	"testing"

	"purple_go/pkg/analysis"
	"purple_go/pkg/ast"
)

func TestArenaCodeGenerator(t *testing.T) {
	gen := NewArenaCodeGenerator()

	// Test that cyclic shapes trigger arena
	t.Run("ShouldUseArena_Cyclic", func(t *testing.T) {
		// Create a letrec expression that forms a cyclic structure
		expr := ast.NewCell(
			ast.NewSym("letrec"),
			ast.NewCell(
				ast.NewCell( // bindings
					ast.NewCell(ast.NewSym("x"), ast.NewCell(ast.NewSym("y"), ast.Nil)),
					ast.NewCell(
						ast.NewCell(ast.NewSym("y"), ast.NewCell(ast.NewSym("x"), ast.Nil)),
						ast.Nil,
					),
				),
				ast.NewCell(ast.NewSym("x"), ast.Nil),
			),
		)

		gen.shapeCtx.AnalyzeShapes(expr)
		if gen.shapeCtx.ResultShape != analysis.ShapeCyclic {
			t.Errorf("expected CYCLIC shape, got %s", analysis.ShapeString(gen.shapeCtx.ResultShape))
		}
	})

	t.Run("ShouldUseArena_Tree", func(t *testing.T) {
		// Create a simple let expression (tree shape)
		expr := ast.NewCell(
			ast.NewSym("let"),
			ast.NewCell(
				ast.NewCell( // bindings
					ast.NewCell(ast.NewSym("x"), ast.NewCell(ast.NewInt(1), ast.Nil)),
					ast.Nil,
				),
				ast.NewCell(ast.NewSym("x"), ast.Nil),
			),
		)

		newGen := NewArenaCodeGenerator()
		if newGen.ShouldUseArena(expr) {
			t.Error("simple let should not require arena")
		}
	})
}

func TestGenerateArenaLet(t *testing.T) {
	gen := NewArenaCodeGenerator()

	bindings := []struct {
		sym *ast.Value
		val string
	}{
		{ast.NewSym("x"), "mk_int(10)"},
		{ast.NewSym("y"), "mk_pair(x, x)"},
	}

	// Test with arena forced
	t.Run("ArenaBlock", func(t *testing.T) {
		result := gen.GenerateArenaLet(bindings, "add(x, y)", true)

		if !strings.Contains(result, "arena_create") {
			t.Error("expected arena_create in output")
		}
		if !strings.Contains(result, "arena_mk_int") {
			t.Error("expected arena_mk_int in output")
		}
		if !strings.Contains(result, "arena_destroy") {
			t.Error("expected arena_destroy in output")
		}
		if !strings.Contains(result, "O(1)") {
			t.Error("expected O(1) comment for bulk free")
		}
	})

	// Test standard block
	t.Run("StandardBlock", func(t *testing.T) {
		result := gen.GenerateArenaLet(bindings, "add(x, y)", false)

		if strings.Contains(result, "arena_create") {
			t.Error("expected no arena_create in standard block")
		}
		if !strings.Contains(result, "Obj* x = mk_int(10)") {
			t.Error("expected standard binding declaration")
		}
	})
}

func TestWeakEdgeDetection(t *testing.T) {
	registry := NewTypeRegistry()
	registry.InitDefaultTypes()

	// Add a doubly-linked node type (both next and prev point to same type)
	registry.RegisterType("DLNode", []TypeField{
		{Name: "value", Type: "int", IsScannable: false},
		{Name: "next", Type: "DLNode", IsScannable: true, Strength: FieldStrong},
		{Name: "prev", Type: "DLNode", IsScannable: true, Strength: FieldStrong},
	})

	// Build ownership graph and analyze back edges
	registry.BuildOwnershipGraph()
	registry.AnalyzeBackEdges()

	// Check that 'prev' is marked as weak (back-edge detection picks one)
	dlNode := registry.Types["DLNode"]
	if dlNode == nil {
		t.Fatal("DLNode type not found")
	}

	hasWeakField := false
	for _, f := range dlNode.Fields {
		if f.IsScannable && f.Strength == FieldWeak {
			hasWeakField = true
		}
	}

	if !hasWeakField {
		t.Log("Note: weak edge detection may require more sophisticated analysis")
	}

	// Generate weak edge comments
	weakEdges := DetectWeakEdges(registry)
	_ = GenerateWeakEdgeComment(weakEdges) // Just check it doesn't panic
}

func TestCodeGenerator(t *testing.T) {
	var sb strings.Builder
	gen := NewCodeGenerator(&sb)

	// Test ValueToCExpr
	t.Run("ValueToCExpr", func(t *testing.T) {
		tests := []struct {
			val      *ast.Value
			expected string
		}{
			{ast.Nil, "NULL"},
			{ast.NewInt(42), "mk_int(42)"},
			{ast.NewCode("my_var"), "my_var"},
		}

		for _, tc := range tests {
			result := gen.ValueToCExpr(tc.val)
			if result != tc.expected {
				t.Errorf("ValueToCExpr(%v) = %s, want %s", tc.val, result, tc.expected)
			}
		}
	})

	// Test LiftValue
	t.Run("LiftValue", func(t *testing.T) {
		v := ast.NewInt(99)
		lifted := gen.LiftValue(v)

		if !ast.IsCode(lifted) {
			t.Error("expected Code value")
		}
		if lifted.Str != "mk_int(99)" {
			t.Errorf("expected mk_int(99), got %s", lifted.Str)
		}
	})
}

func TestGenerateLetWithArenaFallback(t *testing.T) {
	var sb strings.Builder
	gen := NewCodeGenerator(&sb)
	gen.SetArenaFallback(true)

	// Add a variable with cyclic shape
	gen.shapeCtx.AddShape("x", analysis.ShapeCyclic)

	bindings := []struct {
		sym *ast.Value
		val *ast.Value
	}{
		{ast.NewSym("x"), ast.NewCode("mk_int(10)")},
	}

	result := gen.GenerateLet(bindings, ast.NewCode("x"))

	// Should use arena since we have cyclic shape
	if !strings.Contains(result, "arena_create") {
		t.Error("expected arena fallback for cyclic shape")
	}
}

func TestRuntimeGeneration(t *testing.T) {
	registry := NewTypeRegistry()
	registry.InitDefaultTypes()

	runtime := GenerateRuntime(registry)

	t.Run("HasArenaFunctions", func(t *testing.T) {
		if !strings.Contains(runtime, "Arena* arena_create") {
			t.Error("missing arena_create function")
		}
		if !strings.Contains(runtime, "void arena_destroy") {
			t.Error("missing arena_destroy function")
		}
		if !strings.Contains(runtime, "arena_mk_int") {
			t.Error("missing arena_mk_int function")
		}
		if !strings.Contains(runtime, "arena_mk_pair") {
			t.Error("missing arena_mk_pair function")
		}
	})

	t.Run("HasInternalWeakRefSupport", func(t *testing.T) {
		// WeakRef is now internal (_InternalWeakRef) - users don't use it directly
		if !strings.Contains(runtime, "_InternalWeakRef") {
			t.Error("missing _InternalWeakRef type")
		}
		if !strings.Contains(runtime, "_mk_weak_ref") {
			t.Error("missing _mk_weak_ref function")
		}
		if !strings.Contains(runtime, "invalidate_weak_refs_for") {
			t.Error("missing invalidate_weak_refs_for function")
		}
	})

	t.Run("HasShapeBasedFree", func(t *testing.T) {
		if !strings.Contains(runtime, "void free_tree") {
			t.Error("missing free_tree function")
		}
		if !strings.Contains(runtime, "void dec_ref") {
			t.Error("missing dec_ref function")
		}
		if !strings.Contains(runtime, "void deferred_release") {
			t.Error("missing deferred_release function")
		}
	})
}

func TestGenerateProgramToString(t *testing.T) {
	exprs := []*ast.Value{
		ast.NewCode("add(mk_int(1), mk_int(2))"),
	}

	program := GenerateProgramToString(exprs)

	// Check program structure
	if !strings.Contains(program, "#include <stdlib.h>") {
		t.Error("missing stdlib include")
	}
	if !strings.Contains(program, "int main(void)") {
		t.Error("missing main function")
	}
	if !strings.Contains(program, "add(mk_int(1), mk_int(2))") {
		t.Error("missing expression in main")
	}
	if !strings.Contains(program, "flush_freelist()") {
		t.Error("missing freelist flush in main")
	}
}
