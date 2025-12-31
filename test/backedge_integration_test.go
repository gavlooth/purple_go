package test

import (
	"strings"
	"testing"

	"purple_go/pkg/ast"
	"purple_go/pkg/codegen"
	"purple_go/pkg/eval"
	"purple_go/pkg/parser"
)

// TestBackEdgeIntegration tests the complete back-edge detection and codegen pipeline
func TestBackEdgeIntegration(t *testing.T) {
	tests := []struct {
		name           string
		typeDefInput   string
		typeName       string
		expectedWeak   []string // fields that should be weak
		expectedStrong []string // fields that should be strong
	}{
		{
			name: "DoublyLinkedList",
			typeDefInput: `(deftype DLNode
				(value int)
				(next DLNode)
				(prev DLNode))`,
			typeName:       "DLNode",
			expectedWeak:   []string{"prev"},
			expectedStrong: []string{"next"},
		},
		{
			name: "TreeWithParent",
			typeDefInput: `(deftype TreeNode
				(value int)
				(left TreeNode)
				(right TreeNode)
				(parent TreeNode))`,
			typeName:       "TreeNode",
			expectedWeak:   []string{"parent"},
			expectedStrong: []string{"left", "right"},
		},
		{
			name: "GraphNode",
			typeDefInput: `(deftype GNode
				(value int)
				(primary GNode)
				(secondary GNode)
				(backref GNode))`,
			typeName:       "GNode",
			expectedWeak:   []string{"backref"}, // "back" naming hint breaks cycle - only one weak needed
			expectedStrong: []string{"primary", "secondary"},
		},
		{
			name: "ContainerPattern",
			typeDefInput: `(deftype Element
				(value int)
				(owner Container)
				(next Element))`,
			typeName:       "Element",
			// "owner" is weak by naming hint, "next" creates self-loop so DFS marks it weak
			expectedWeak:   []string{"owner", "next"},
			expectedStrong: []string{},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			// Reset registry
			codegen.ResetGlobalRegistry()

			// Parse and eval the deftype
			p := parser.New(tc.typeDefInput)
			expr, err := p.Parse()
			if err != nil {
				t.Fatalf("Parse error: %v", err)
			}

			env := eval.DefaultEnv()
			menv := eval.NewMenv(ast.Nil, env)
			result := eval.Eval(expr, menv)

			if ast.IsError(result) {
				t.Fatalf("Eval error: %s", result.Str)
			}

			registry := codegen.GlobalRegistry()
			typeDef := registry.FindType(tc.typeName)
			if typeDef == nil {
				t.Fatalf("Type %s not found in registry", tc.typeName)
			}

			// Check expected weak fields
			for _, fieldName := range tc.expectedWeak {
				if !registry.IsFieldWeak(tc.typeName, fieldName) {
					t.Errorf("Field %s.%s should be weak", tc.typeName, fieldName)
				}
			}

			// Check expected strong fields
			for _, fieldName := range tc.expectedStrong {
				if !registry.IsFieldStrong(tc.typeName, fieldName) {
					t.Errorf("Field %s.%s should be strong", tc.typeName, fieldName)
				}
			}

			// Generate runtime and check output
			runtime := codegen.GenerateRuntime(registry)

			// Check that release function exists
			releaseFn := "void release_" + tc.typeName
			if !strings.Contains(runtime, releaseFn) {
				t.Errorf("Missing release function: %s", releaseFn)
			}

			// Check that weak fields are skipped in release
			for _, fieldName := range tc.expectedWeak {
				skipComment := fieldName + ": weak back-edge"
				if !strings.Contains(runtime, skipComment) {
					t.Errorf("Release function should skip weak field %s", fieldName)
				}
			}

			// Check that strong fields are decremented
			for _, fieldName := range tc.expectedStrong {
				// Look for dec_ref call for this field
				decRefCall := "dec_ref((Obj*)x->" + fieldName + ")"
				if !strings.Contains(runtime, decRefCall) {
					t.Errorf("Release function should dec_ref strong field %s", fieldName)
				}
			}
		})
	}
}

// TestBackEdgeCycleStatus tests that cycle detection works correctly
func TestBackEdgeCycleStatus(t *testing.T) {
	tests := []struct {
		name           string
		typeDefInput   string
		typeName       string
		expectedStatus codegen.CycleStatus
	}{
		{
			name: "NonRecursiveType",
			typeDefInput: `(deftype Simple
				(value int)
				(data int))`,
			typeName:       "Simple",
			expectedStatus: codegen.CycleStatusNone,
		},
		{
			name: "BrokenCycleByNaming",
			typeDefInput: `(deftype LinkedNode
				(value int)
				(next LinkedNode)
				(prev LinkedNode))`,
			typeName:       "LinkedNode",
			expectedStatus: codegen.CycleStatusBroken,
		},
		{
			name: "SelfReferentialWithHint",
			typeDefInput: `(deftype Child
				(value int)
				(parent Child))`,
			typeName:       "Child",
			expectedStatus: codegen.CycleStatusBroken, // "parent" hint breaks cycle
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			codegen.ResetGlobalRegistry()

			p := parser.New(tc.typeDefInput)
			expr, _ := p.Parse()

			env := eval.DefaultEnv()
			menv := eval.NewMenv(ast.Nil, env)
			eval.Eval(expr, menv)

			registry := codegen.GlobalRegistry()
			status := registry.GetCycleStatus(tc.typeName)

			if status != tc.expectedStatus {
				t.Errorf("Expected cycle status %d, got %d", tc.expectedStatus, status)
			}
		})
	}
}

// TestBackEdgeRuntimeGeneration tests the complete runtime output
func TestBackEdgeRuntimeGeneration(t *testing.T) {
	codegen.ResetGlobalRegistry()

	// Define a complex type hierarchy
	inputs := []string{
		`(deftype Container (items ItemList) (owner Container))`,
		`(deftype ItemList (head Item) (tail ItemList))`,
		`(deftype Item (value int) (container Container) (prev Item))`,
	}

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)

	for _, input := range inputs {
		p := parser.New(input)
		expr, _ := p.Parse()
		eval.Eval(expr, menv)
	}

	registry := codegen.GlobalRegistry()
	runtime := codegen.GenerateRuntime(registry)

	// Verify essential components
	checks := []struct {
		description string
		contains    string
	}{
		{"Internal weak ref type", "_InternalWeakRef"},
		{"Invalidate weak refs", "invalidate_weak_refs_for"},
		{"User types section", "User-Defined Types"},
		{"Container type", "typedef struct Container"},
		{"Item type", "typedef struct Item"},
		{"ItemList type", "typedef struct ItemList"},
		{"Release functions", "Type-Aware Release Functions"},
		{"Field accessors", "Field Accessors"},
		// New integrated features
		{"Arena with externals", "arena_register_external"},
		{"Arena reset", "arena_reset"},
		{"SCC-based RC", "SCC-Based Reference Counting"},
		{"Tarjan algorithm", "TarjanState"},
		{"SCC detection", "detect_and_freeze_sccs"},
		{"Release with SCC", "release_with_scc"},
		{"Deferred RC", "Deferred Reference Counting"},
		{"Defer decrement", "defer_decrement"},
		{"Process deferred", "process_deferred"},
		{"Safe point", "safe_point"},
		{"Perceus reuse", "try_reuse"},
	}

	for _, check := range checks {
		if !strings.Contains(runtime, check.contains) {
			t.Errorf("Missing: %s (looking for '%s')", check.description, check.contains)
		}
	}

	// Verify weak fields are properly marked
	weakFields := []struct {
		typeName  string
		fieldName string
	}{
		{"Container", "owner"},   // naming hint
		{"Item", "container"},    // naming hint
		{"Item", "prev"},         // naming hint
	}

	for _, wf := range weakFields {
		if !registry.IsFieldWeak(wf.typeName, wf.fieldName) {
			t.Errorf("%s.%s should be weak", wf.typeName, wf.fieldName)
		}
	}

	t.Logf("Generated runtime is %d bytes", len(runtime))
}

// TestNoWeakRefExposure verifies that WeakRef is internal only
func TestNoWeakRefExposure(t *testing.T) {
	codegen.ResetGlobalRegistry()

	input := `(deftype Node (value int) (next Node) (prev Node))`
	p := parser.New(input)
	expr, _ := p.Parse()

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)
	eval.Eval(expr, menv)

	registry := codegen.GlobalRegistry()
	runtime := codegen.GenerateRuntime(registry)

	// WeakRef should be internal (prefixed with underscore)
	if strings.Contains(runtime, "WeakRef* mk_weak_ref") {
		t.Error("Public mk_weak_ref should not exist - WeakRef is internal")
	}

	// Internal version should exist
	if !strings.Contains(runtime, "_InternalWeakRef* _mk_weak_ref") {
		t.Error("Internal _mk_weak_ref should exist")
	}

	// Comment should indicate internal use only
	if !strings.Contains(runtime, "internal to the runtime") {
		t.Error("WeakRef section should indicate internal use")
	}

	// User-defined type should NOT use WeakRef directly
	if strings.Contains(runtime, "WeakRef* prev") {
		t.Error("User types should not use WeakRef directly")
	}
}
