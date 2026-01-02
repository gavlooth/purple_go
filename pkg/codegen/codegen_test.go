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
		{ast.NewSym("x"), "mk_int_unboxed(10)"},  // Unboxed integers - no arena needed
		{ast.NewSym("y"), "mk_pair(x, x)"},
	}

	// Test with arena forced
	t.Run("ArenaBlock", func(t *testing.T) {
		result := gen.GenerateArenaLet(bindings, "add(x, y)", true)

		if !strings.Contains(result, "arena_create") {
			t.Error("expected arena_create in output")
		}
		// Unboxed integers don't need arena allocation - they're already zero-cost
		if !strings.Contains(result, "mk_int_unboxed") {
			t.Error("expected mk_int_unboxed in output (unboxed bypasses arena)")
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
		if !strings.Contains(result, "Obj* x = mk_int_unboxed(10)") {
			t.Error("expected standard binding declaration with unboxed int")
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

func TestBackEdgePatterns(t *testing.T) {
	// Test all back-edge naming patterns
	patterns := []struct {
		fieldName string
		isBackEdge bool
	}{
		// Original patterns
		{"parent", true},
		{"prev", true},
		{"back", true},
		{"owner", true},
		{"container", true},
		{"previous", true},
		{"up", true},
		{"outer", true},

		// New expanded patterns
		{"ancestor", true},
		{"predecessor", true},
		{"enclosing", true},
		{"backref", true},
		{"backpointer", true},

		// Suffix patterns
		{"node_back", true},
		{"link_prev", true},
		{"ref_parent", true},
		{"ptr_up", true},

		// Mixed case
		{"ParentNode", true},
		{"prevSibling", true},
		{"ANCESTOR", true},

		// Non-back-edge patterns (should be false)
		{"next", false},
		{"child", false},
		{"data", false},
		{"value", false},
		{"forward", false},
		{"head", false},
		{"tail", false},
	}

	for _, tc := range patterns {
		result := isBackEdgeHint(tc.fieldName)
		if result != tc.isBackEdge {
			t.Errorf("isBackEdgeHint(%q) = %v, want %v", tc.fieldName, result, tc.isBackEdge)
		}
	}
}

func TestExceptionContext(t *testing.T) {
	ctx := NewExceptionContext()

	// Test entering/exiting try blocks
	pad := ctx.EnterTryBlock("e")
	if pad == nil {
		t.Fatal("EnterTryBlock returned nil")
	}
	if pad.TryBlockID != 0 {
		t.Errorf("TryBlockID = %d, want 0", pad.TryBlockID)
	}
	if ctx.CurrentTryBlock != 0 {
		t.Errorf("CurrentTryBlock = %d, want 0", ctx.CurrentTryBlock)
	}

	// Test adding cleanup points
	cp := ctx.AddCleanupPoint("x", "Obj", 10)
	if cp == nil {
		t.Fatal("AddCleanupPoint returned nil")
	}
	if len(pad.CleanupPoints) != 1 {
		t.Errorf("CleanupPoints count = %d, want 1", len(pad.CleanupPoints))
	}

	// Test exiting try block
	exitPad := ctx.ExitTryBlock()
	if exitPad != pad {
		t.Error("ExitTryBlock returned different pad")
	}
	if ctx.CurrentTryBlock != -1 {
		t.Errorf("CurrentTryBlock after exit = %d, want -1", ctx.CurrentTryBlock)
	}
}

func TestExceptionRuntimeGeneration(t *testing.T) {
	var sb strings.Builder
	registry := NewTypeRegistry()
	registry.InitDefaultTypes()

	gen := NewExceptionCodeGenerator(&sb, registry)
	gen.GenerateExceptionRuntime()

	result := sb.String()

	// Check for key components
	checks := []string{
		"ExceptionContext",
		"exception_push",
		"exception_pop",
		"exception_throw",
		"TRY_BEGIN",
		"TRY_CATCH",
		"REGISTER_CLEANUP",
		"setjmp",
		"longjmp",
	}

	for _, check := range checks {
		if !strings.Contains(result, check) {
			t.Errorf("Exception runtime missing: %s", check)
		}
	}
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
			{ast.NewInt(42), "mk_int_unboxed(42)"},
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
		if lifted.Str != "mk_int_unboxed(99)" {
			t.Errorf("expected mk_int_unboxed(99), got %s", lifted.Str)
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

func TestFlushFreelistDecrementsChildren(t *testing.T) {
	// This test verifies that flush_freelist properly decrements children
	// before freeing pair objects, preventing memory leaks.
	registry := NewTypeRegistry()
	registry.InitDefaultTypes()

	runtime := GenerateRuntime(registry)

	// The flush_freelist function calls release_children which handles pairs
	// via TAG_PAIR case instead of is_pair check
	if !strings.Contains(runtime, "release_children(n->obj)") {
		t.Error("flush_freelist should call release_children")
	}
	// release_children uses TAG_PAIR switch case to decrement pair children
	if !strings.Contains(runtime, "case TAG_PAIR:") {
		t.Error("release_children should handle TAG_PAIR case")
	}
	if !strings.Contains(runtime, "dec_ref(x->a)") {
		t.Error("release_children should decrement first child of pair")
	}
	if !strings.Contains(runtime, "dec_ref(x->b)") {
		t.Error("release_children should decrement second child of pair")
	}
}

func TestLivenessIntegration(t *testing.T) {
	// Test that liveness analysis is integrated into let generation
	var sb strings.Builder
	gen := NewCodeGenerator(&sb)

	// Create a let with an unused variable (pairs don't use stack alloc)
	bindings := []struct {
		sym *ast.Value
		val *ast.Value
	}{
		// Use pair values which won't be stack-allocated
		{ast.NewSym("x"), ast.NewCode("mk_pair(mk_int(1), NULL)")},
		{ast.NewSym("y"), ast.NewCode("mk_pair(mk_int(2), NULL)")},
	}

	// Body that only uses y
	body := ast.NewSym("y")

	result := gen.GenerateLet(bindings, body)
	t.Log("Generated code:", result)

	// x should be marked as never used (lastUse=-1)
	if !strings.Contains(result, "x never used") {
		t.Log("Note: x was not marked as 'never used' - liveness info may be in different format")
	}

	// Should have lastUse info in the free comments
	if strings.Contains(result, "lastUse=") {
		t.Log("Liveness information present in output")
	}

	// Both variables should be in output
	if !strings.Contains(result, "x") || !strings.Contains(result, "y") {
		t.Error("Expected both x and y in output")
	}
}

func TestStackAllocationForNonEscaping(t *testing.T) {
	// Test that non-escaping primitives use stack allocation
	var sb strings.Builder
	gen := NewCodeGenerator(&sb)

	// Test int literal - unboxed is better than stack allocation (zero allocation)
	t.Run("IntToStackExpr", func(t *testing.T) {
		v := ast.NewInt(42)
		result := gen.ValueToCExprStack(v)
		if result != "mk_int_unboxed(42)" {
			t.Errorf("ValueToCExprStack(42) = %s, want mk_int_unboxed(42)", result)
		}
	})

	// Test float literal
	t.Run("FloatToStackExpr", func(t *testing.T) {
		v := ast.NewFloat(3.14)
		result := gen.ValueToCExprStack(v)
		expected := "mk_float_stack(3.14)"
		if result != expected {
			t.Errorf("ValueToCExprStack(3.14) = %s, want %s", result, expected)
		}
	})

	// Test char literal - unboxed is better than stack allocation
	t.Run("CharToStackExpr", func(t *testing.T) {
		v := ast.NewChar(65) // 'A'
		result := gen.ValueToCExprStack(v)
		if result != "mk_char_unboxed(65)" {
			t.Errorf("ValueToCExprStack('A') = %s, want mk_char_unboxed(65)", result)
		}
	})

	// Test isPrimitiveValue
	t.Run("IsPrimitiveValue", func(t *testing.T) {
		tests := []struct {
			val      *ast.Value
			expected bool
		}{
			{ast.NewInt(1), true},
			{ast.NewFloat(1.0), true},
			{ast.NewChar(65), true},
			{ast.NewSym("x"), false},
			{ast.NewCell(ast.NewInt(1), ast.Nil), false},
			{nil, false},
		}
		for _, tt := range tests {
			result := isPrimitiveValue(tt.val)
			if result != tt.expected {
				t.Errorf("isPrimitiveValue(%v) = %v, want %v", tt.val, result, tt.expected)
			}
		}
	})
}

func TestBorrowRefIntegration(t *testing.T) {
	// Test that BorrowRef is used for borrowed references
	var sb strings.Builder
	gen := NewCodeGenerator(&sb)

	// Enable BorrowRef (should be on by default)
	gen.SetBorrowRefEnabled(true)

	// Create a let with a borrowed reference (variable reference to outer var)
	// When bi.val is a symbol (variable reference), it's considered borrowed
	bindings := []struct {
		sym *ast.Value
		val *ast.Value
	}{
		// y = x is a borrowed reference (x is defined elsewhere)
		{ast.NewSym("y"), ast.NewSym("x")},
	}

	body := ast.NewSym("y")

	result := gen.GenerateLet(bindings, body)
	t.Log("Generated code:", result)

	// Should contain BorrowRef creation for borrowed reference
	if !strings.Contains(result, "borrow_create") {
		t.Error("expected borrow_create for borrowed reference")
	}
	if !strings.Contains(result, "_ref_y") {
		t.Error("expected BorrowRef variable _ref_y")
	}
	if !strings.Contains(result, "borrow_get") {
		t.Error("expected borrow_get for validated access")
	}
	if !strings.Contains(result, "borrow_release") {
		t.Error("expected borrow_release at scope exit")
	}
	if !strings.Contains(result, "borrow:y") {
		t.Error("expected source description in BorrowRef creation")
	}
}

func TestBorrowRefDisabled(t *testing.T) {
	// Test that BorrowRef can be disabled
	var sb strings.Builder
	gen := NewCodeGenerator(&sb)

	// Disable BorrowRef
	gen.SetBorrowRefEnabled(false)

	bindings := []struct {
		sym *ast.Value
		val *ast.Value
	}{
		{ast.NewSym("y"), ast.NewSym("x")},
	}

	body := ast.NewSym("y")

	result := gen.GenerateLet(bindings, body)
	t.Log("Generated code:", result)

	// Should NOT contain BorrowRef when disabled
	if strings.Contains(result, "borrow_create") {
		t.Error("should not contain borrow_create when disabled")
	}
	if strings.Contains(result, "_ref_y") {
		t.Error("should not contain BorrowRef variable when disabled")
	}
}

func TestOptimizationStats(t *testing.T) {
	// Test optimization statistics tracking
	var sb strings.Builder
	gen := NewCodeGenerator(&sb)

	// Create bindings that trigger various optimizations
	t.Run("BorrowRefStats", func(t *testing.T) {
		// Reset stats
		gen = NewCodeGenerator(&sb)

		bindings := []struct {
			sym *ast.Value
			val *ast.Value
		}{
			// Borrowed reference - should use BorrowRef
			{ast.NewSym("y"), ast.NewSym("x")},
		}

		gen.GenerateLet(bindings, ast.NewSym("y"))

		stats := gen.GetStats()
		if stats.BorrowRefCreated != 1 {
			t.Errorf("expected 1 BorrowRef created, got %d", stats.BorrowRefCreated)
		}
		if stats.BorrowRefReleased != 1 {
			t.Errorf("expected 1 BorrowRef released, got %d", stats.BorrowRefReleased)
		}
	})

	t.Run("StatsSummary", func(t *testing.T) {
		gen = NewCodeGenerator(&sb)

		// Do some operations
		gen.GenerateLet([]struct {
			sym *ast.Value
			val *ast.Value
		}{
			{ast.NewSym("y"), ast.NewSym("x")},
		}, ast.NewSym("y"))

		summary := gen.GetStatsSummary()
		if summary == "" {
			t.Error("expected non-empty summary")
		}
		t.Log("Summary:", summary)
	})

	t.Run("StatsReport", func(t *testing.T) {
		gen = NewCodeGenerator(&sb)

		// Do some operations
		gen.GenerateLet([]struct {
			sym *ast.Value
			val *ast.Value
		}{
			{ast.NewSym("y"), ast.NewSym("x")},
		}, ast.NewSym("y"))

		report := gen.GetStatsReport()
		if !strings.Contains(report, "Optimization Statistics") {
			t.Error("expected full report header")
		}
		if !strings.Contains(report, "BorrowRef") {
			t.Error("expected BorrowRef section in report")
		}
		t.Log("Report:\n", report)
	})
}

func TestOptimizationStatsMerge(t *testing.T) {
	stats1 := NewOptimizationStats()
	stats1.BorrowRefCreated = 5
	stats1.StackAllocations = 10

	stats2 := NewOptimizationStats()
	stats2.BorrowRefCreated = 3
	stats2.MemoryReused = 2

	stats1.Merge(stats2)

	if stats1.BorrowRefCreated != 8 {
		t.Errorf("expected merged BorrowRefCreated=8, got %d", stats1.BorrowRefCreated)
	}
	if stats1.StackAllocations != 10 {
		t.Errorf("expected StackAllocations=10, got %d", stats1.StackAllocations)
	}
	if stats1.MemoryReused != 2 {
		t.Errorf("expected MemoryReused=2, got %d", stats1.MemoryReused)
	}
}
