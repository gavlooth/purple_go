package analysis

import (
	"testing"
)

func TestOwnershipContext_Basic(t *testing.T) {
	ctx := NewOwnershipContext(nil)

	// Test DefineOwned
	ctx.DefineOwned("x")
	info := ctx.GetOwnership("x")
	if info == nil {
		t.Fatal("Expected ownership info for x")
	}
	if info.Class != OwnerLocal {
		t.Errorf("Expected OwnerLocal, got %v", info.Class)
	}
	if info.ConsumedAt != -1 {
		t.Errorf("Expected ConsumedAt=-1, got %d", info.ConsumedAt)
	}

	// Test DefineBorrowed
	ctx.DefineBorrowed("y")
	info = ctx.GetOwnership("y")
	if info.Class != OwnerBorrowed {
		t.Errorf("Expected OwnerBorrowed, got %v", info.Class)
	}
}

func TestOwnershipContext_Consume(t *testing.T) {
	ctx := NewOwnershipContext(nil)

	// Create owned variable
	ctx.DefineOwned("x")
	if ctx.IsConsumed("x") {
		t.Error("x should not be consumed initially")
	}

	// Consume it
	ctx.ConsumeOwnership("x", "send")
	if !ctx.IsConsumed("x") {
		t.Error("x should be consumed after ConsumeOwnership")
	}
	if ctx.GetConsumer("x") != "send" {
		t.Errorf("Expected consumer 'send', got '%s'", ctx.GetConsumer("x"))
	}

	// Consumed values should not be freed
	if ctx.ShouldFree("x") {
		t.Error("Consumed variable should not be freed")
	}
}

func TestOwnershipContext_NeedsIncRef(t *testing.T) {
	ctx := NewOwnershipContext(nil)
	ctx.DefineOwned("x")

	// Borrowed params skip inc_ref
	if ctx.NeedsIncRef("x", OwnerBorrowed) {
		t.Error("Borrowed params should not need inc_ref")
	}

	// Consumed params skip inc_ref
	if ctx.NeedsIncRef("x", OwnerConsumed) {
		t.Error("Consumed params should not need inc_ref")
	}

	// Shared params need inc_ref
	if !ctx.NeedsIncRef("x", OwnerShared) {
		t.Error("Shared params should need inc_ref")
	}
}

func TestOwnershipContext_NeedsDecRef(t *testing.T) {
	ctx := NewOwnershipContext(nil)

	// Owned needs dec_ref
	ctx.DefineOwned("x")
	if !ctx.NeedsDecRef("x") {
		t.Error("Owned variable should need dec_ref")
	}

	// Borrowed does not need dec_ref
	ctx.DefineBorrowed("y")
	if ctx.NeedsDecRef("y") {
		t.Error("Borrowed variable should not need dec_ref")
	}

	// Consumed does not need dec_ref
	ctx.DefineOwned("z")
	ctx.ConsumeOwnership("z", "callee")
	if ctx.NeedsDecRef("z") {
		t.Error("Consumed variable should not need dec_ref")
	}
}

func TestOwnershipContext_PureContext(t *testing.T) {
	ctx := NewOwnershipContext(nil)
	ctx.DefineOwned("x")

	// Normal variable needs dec_ref
	if !ctx.NeedsDecRef("x") {
		t.Error("Normal owned variable should need dec_ref")
	}

	// Mark as pure
	ctx.MarkAsPure("x")
	if ctx.NeedsDecRef("x") {
		t.Error("Pure variable should not need dec_ref")
	}
}

func TestOwnershipContext_UseCount(t *testing.T) {
	ctx := NewOwnershipContext(nil)
	ctx.DefineOwned("x")

	if ctx.GetUseCount("x") != 0 {
		t.Error("Initial use count should be 0")
	}

	ctx.IncrementUseCount("x")
	if ctx.GetUseCount("x") != 1 {
		t.Errorf("Expected use count 1, got %d", ctx.GetUseCount("x"))
	}
	if !ctx.IsSingleUse("x") {
		t.Error("Variable used once should be single-use")
	}

	ctx.IncrementUseCount("x")
	if ctx.IsSingleUse("x") {
		t.Error("Variable used twice should not be single-use")
	}
}

func TestOwnershipMode(t *testing.T) {
	ctx := NewOwnershipContext(nil)

	// Owned mode
	ctx.DefineOwned("x")
	if ctx.GetOwnershipMode("x") != ModeOwned {
		t.Error("Expected ModeOwned for owned variable")
	}

	// Borrowed mode
	ctx.DefineBorrowed("y")
	if ctx.GetOwnershipMode("y") != ModeBorrowed {
		t.Error("Expected ModeBorrowed for borrowed variable")
	}

	// Consumed mode
	ctx.DefineOwned("z")
	ctx.ConsumeOwnership("z", "callee")
	if ctx.GetOwnershipMode("z") != ModeConsumed {
		t.Error("Expected ModeConsumed for consumed variable")
	}

	// Transferred mode (should map to consumed)
	ctx.DefineOwned("w")
	ctx.TransferOwnership("w", "target")
	if ctx.GetOwnershipMode("w") != ModeConsumed {
		t.Error("Expected ModeConsumed for transferred variable")
	}
}

func TestOwnershipClassString(t *testing.T) {
	tests := []struct {
		class    OwnershipClass
		expected string
	}{
		{OwnerLocal, "local"},
		{OwnerBorrowed, "borrowed"},
		{OwnerTransferred, "transferred"},
		{OwnerShared, "shared"},
		{OwnerWeak, "weak"},
		{OwnerConsumed, "consumed"},
		{OwnerUnknown, "unknown"},
	}

	for _, tc := range tests {
		got := OwnershipClassString(tc.class)
		if got != tc.expected {
			t.Errorf("OwnershipClassString(%v) = %s, want %s", tc.class, got, tc.expected)
		}
	}
}

func TestOwnershipModeString(t *testing.T) {
	tests := []struct {
		mode     OwnershipMode
		expected string
	}{
		{ModeOwned, "owned"},
		{ModeBorrowed, "borrowed"},
		{ModeConsumed, "consumed"},
	}

	for _, tc := range tests {
		got := OwnershipModeString(tc.mode)
		if got != tc.expected {
			t.Errorf("OwnershipModeString(%v) = %s, want %s", tc.mode, got, tc.expected)
		}
	}
}

func TestOwnershipContext_ScopeStack(t *testing.T) {
	ctx := NewOwnershipContext(nil)

	if ctx.CurrentScope() != "global" {
		t.Error("Initial scope should be 'global'")
	}

	ctx.EnterScope("let")
	if ctx.CurrentScope() != "let" {
		t.Errorf("Expected scope 'let', got '%s'", ctx.CurrentScope())
	}

	ctx.EnterScope("lambda")
	if ctx.CurrentScope() != "lambda" {
		t.Errorf("Expected scope 'lambda', got '%s'", ctx.CurrentScope())
	}

	ctx.ExitScope()
	if ctx.CurrentScope() != "let" {
		t.Errorf("Expected scope 'let' after exit, got '%s'", ctx.CurrentScope())
	}

	ctx.ExitScope()
	if ctx.CurrentScope() != "global" {
		t.Errorf("Expected scope 'global' after exit, got '%s'", ctx.CurrentScope())
	}
}
