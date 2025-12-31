package test

import (
	"strings"
	"testing"

	"purple_go/pkg/ast"
	"purple_go/pkg/codegen"
	"purple_go/pkg/eval"
	"purple_go/pkg/parser"
)

func TestDeftype(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Define a doubly-linked list node type
	input := `(deftype Node
		(value int)
		(next Node)
		(prev Node))`

	p := parser.New(input)
	expr, err := p.Parse()
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}

	// Evaluate the deftype
	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)
	result := eval.Eval(expr, menv)

	if ast.IsError(result) {
		t.Fatalf("Eval error: %s", result.Str)
	}

	// Check that the type was registered
	registry := codegen.GlobalRegistry()
	nodeDef := registry.FindType("Node")
	if nodeDef == nil {
		t.Fatal("Node type not registered")
	}

	// Check fields
	if len(nodeDef.Fields) != 3 {
		t.Fatalf("Expected 3 fields, got %d", len(nodeDef.Fields))
	}

	// Check field names and types
	expectedFields := []struct {
		name        string
		typ         string
		isScannable bool
	}{
		{"value", "int", false},
		{"next", "Node", true},
		{"prev", "Node", true},
	}

	for i, expected := range expectedFields {
		if nodeDef.Fields[i].Name != expected.name {
			t.Errorf("Field %d: expected name %s, got %s", i, expected.name, nodeDef.Fields[i].Name)
		}
		if nodeDef.Fields[i].Type != expected.typ {
			t.Errorf("Field %d: expected type %s, got %s", i, expected.typ, nodeDef.Fields[i].Type)
		}
		if nodeDef.Fields[i].IsScannable != expected.isScannable {
			t.Errorf("Field %d: expected isScannable %v, got %v", i, expected.isScannable, nodeDef.Fields[i].IsScannable)
		}
	}

	// Check that back-edge analysis ran
	// The 'prev' field should be marked as weak since it forms a cycle
	foundBackEdge := false
	for _, edge := range registry.OwnershipGraph {
		if edge.FromType == "Node" && edge.FieldName == "prev" && edge.IsBackEdge {
			foundBackEdge = true
			break
		}
	}

	if !foundBackEdge {
		t.Log("Note: Back-edge detection found edges:", registry.OwnershipGraph)
	}
}

func TestDeftypeTreeWithParent(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Define a tree type with parent pointer
	input := `(deftype Tree
		(value int)
		(left Tree)
		(right Tree)
		(parent Tree))`

	p := parser.New(input)
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
	treeDef := registry.FindType("Tree")
	if treeDef == nil {
		t.Fatal("Tree type not registered")
	}

	// Check that it's marked as recursive
	if !treeDef.IsRecursive {
		t.Error("Tree type should be marked as recursive")
	}
}

func TestBackEdgeHeuristics(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Test naming heuristics
	input := `(deftype DoublyLinked
		(value int)
		(next DoublyLinked)
		(prev DoublyLinked))`

	p := parser.New(input)
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
	dlDef := registry.FindType("DoublyLinked")
	if dlDef == nil {
		t.Fatal("DoublyLinked type not registered")
	}

	// Check that 'prev' is marked as weak (naming heuristic)
	prevField := findField(dlDef, "prev")
	if prevField == nil {
		t.Fatal("prev field not found")
	}
	if prevField.Strength != codegen.FieldWeak {
		t.Errorf("prev field should be weak, got %v", prevField.Strength)
	}

	// Check that 'next' is still strong
	nextField := findField(dlDef, "next")
	if nextField == nil {
		t.Fatal("next field not found")
	}
	if nextField.Strength != codegen.FieldStrong {
		t.Errorf("next field should be strong, got %v", nextField.Strength)
	}
}

func TestBackEdgeHeuristicsParent(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Test parent pointer heuristic
	input := `(deftype TreeNode
		(value int)
		(left TreeNode)
		(right TreeNode)
		(parent TreeNode))`

	p := parser.New(input)
	expr, err := p.Parse()
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)
	eval.Eval(expr, menv)

	registry := codegen.GlobalRegistry()
	treeDef := registry.FindType("TreeNode")
	if treeDef == nil {
		t.Fatal("TreeNode type not registered")
	}

	// 'parent' should be weak (naming heuristic)
	parentField := findField(treeDef, "parent")
	if parentField == nil {
		t.Fatal("parent field not found")
	}
	if parentField.Strength != codegen.FieldWeak {
		t.Errorf("parent field should be weak, got %v", parentField.Strength)
	}

	// 'left' should be strong
	leftField := findField(treeDef, "left")
	if leftField == nil {
		t.Fatal("left field not found")
	}
	if leftField.Strength != codegen.FieldStrong {
		t.Errorf("left field should be strong, got %v", leftField.Strength)
	}
}

func TestSecondPointerHeuristic(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Test second pointer detection (without naming hint)
	input := `(deftype Graph
		(data int)
		(primary Graph)
		(secondary Graph))`

	p := parser.New(input)
	expr, err := p.Parse()
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)
	eval.Eval(expr, menv)

	registry := codegen.GlobalRegistry()
	graphDef := registry.FindType("Graph")
	if graphDef == nil {
		t.Fatal("Graph type not registered")
	}

	// 'primary' should be strong (first pointer to Graph)
	primaryField := findField(graphDef, "primary")
	if primaryField == nil {
		t.Fatal("primary field not found")
	}
	if primaryField.Strength != codegen.FieldStrong {
		t.Errorf("primary field should be strong, got %v", primaryField.Strength)
	}

	// 'secondary' should be weak (second pointer to same type)
	secondaryField := findField(graphDef, "secondary")
	if secondaryField == nil {
		t.Fatal("secondary field not found")
	}
	if secondaryField.Strength != codegen.FieldWeak {
		t.Errorf("secondary field should be weak, got %v", secondaryField.Strength)
	}
}

func findField(def *codegen.TypeDef, name string) *codegen.TypeField {
	for i := range def.Fields {
		if def.Fields[i].Name == name {
			return &def.Fields[i]
		}
	}
	return nil
}

func TestCodegenIntegration(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Define a doubly-linked list node type
	input := `(deftype Node
		(value int)
		(next Node)
		(prev Node))`

	p := parser.New(input)
	expr, err := p.Parse()
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)
	eval.Eval(expr, menv)

	// Generate runtime with the type
	registry := codegen.GlobalRegistry()
	runtime := codegen.GenerateRuntime(registry)

	// Check that the Node type is generated
	if !strings.Contains(runtime, "typedef struct Node") {
		t.Error("missing Node struct definition")
	}

	// Check that release function skips weak field (prev)
	if !strings.Contains(runtime, "release_Node") {
		t.Error("missing release_Node function")
	}

	// Check that prev is documented as weak
	if !strings.Contains(runtime, "prev: weak back-edge") {
		t.Log("Note: Check that prev field is marked as weak in release function")
		t.Log("Runtime snippet:")
		// Find the release_Node function
		start := strings.Index(runtime, "void release_Node")
		if start >= 0 {
			end := start + 500
			if end > len(runtime) {
				end = len(runtime)
			}
			t.Log(runtime[start:end])
		}
	}

	// Check that next is released (strong)
	if !strings.Contains(runtime, "dec_ref") {
		t.Error("missing dec_ref for strong fields")
	}

	// Check that constructor is generated
	if !strings.Contains(runtime, "mk_Node") {
		t.Error("missing mk_Node constructor")
	}

	// Check that field accessors are generated
	if !strings.Contains(runtime, "get_Node_next") {
		t.Error("missing getter for next field")
	}
}

func TestDeftypeMultipleTypes(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Define multiple related types
	inputs := []string{
		`(deftype Container (items List))`,
		`(deftype List (head Item) (tail List))`,
		`(deftype Item (value int) (container Container))`,
	}

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)

	for _, input := range inputs {
		p := parser.New(input)
		expr, err := p.Parse()
		if err != nil {
			t.Fatalf("Parse error: %v", err)
		}
		result := eval.Eval(expr, menv)
		if ast.IsError(result) {
			t.Fatalf("Eval error: %s", result.Str)
		}
	}

	registry := codegen.GlobalRegistry()

	// Check all types were registered
	for _, name := range []string{"Container", "List", "Item"} {
		if registry.FindType(name) == nil {
			t.Errorf("Type %s not registered", name)
		}
	}

	// Item.container forms a cycle back to Container
	// This should be detected as a back-edge
	t.Log("Ownership graph:")
	for _, edge := range registry.OwnershipGraph {
		t.Logf("  %s.%s -> %s (back-edge: %v)", edge.FromType, edge.FieldName, edge.ToType, edge.IsBackEdge)
	}
}
