package analysis

import (
	"testing"
)

func TestPoolContext_Basic(t *testing.T) {
	ctx := NewPoolContext()

	// Add a non-escaping variable
	ctx.EscapeCtx.AddVar("x")

	eligibility := ctx.AnalyzePoolEligibility("x", "int", nil)
	if eligibility == PoolIneligible {
		t.Error("Non-escaping variable should be pool-eligible")
	}

	if !ctx.IsPoolEligible("x") {
		t.Error("IsPoolEligible should return true for eligible variable")
	}
}

func TestPoolContext_EscapingVariable(t *testing.T) {
	ctx := NewPoolContext()

	// Add a variable that escapes
	ctx.EscapeCtx.AddVar("y")
	varUsage := ctx.EscapeCtx.FindVar("y")
	if varUsage != nil {
		varUsage.Escape = EscapeGlobal
	}

	eligibility := ctx.AnalyzePoolEligibility("y", "pair", nil)
	if eligibility != PoolIneligible {
		t.Error("Escaping variable should be pool-ineligible")
	}

	candidate := ctx.GetPoolCandidate("y")
	if candidate == nil {
		t.Fatal("Expected candidate info for y")
	}
	if candidate.Reason != "escapes scope" {
		t.Errorf("Expected reason 'escapes scope', got '%s'", candidate.Reason)
	}
}

func TestPoolContext_CapturedVariable(t *testing.T) {
	ctx := NewPoolContext()

	// Add a variable that is captured by lambda
	ctx.EscapeCtx.AddVar("z")
	varUsage := ctx.EscapeCtx.FindVar("z")
	if varUsage != nil {
		varUsage.CapturedByLambda = true
	}

	eligibility := ctx.AnalyzePoolEligibility("z", "box", nil)
	if eligibility != PoolIneligible {
		t.Error("Captured variable should be pool-ineligible")
	}
}

func TestPoolContext_PureContext(t *testing.T) {
	ctx := NewPoolContext()
	ctx.PurityCtx = NewPurityContext()
	ctx.PurityCtx.EnterPureContext([]string{"p"})

	ctx.EscapeCtx.AddVar("p")

	eligibility := ctx.AnalyzePoolEligibility("p", "int", nil)
	if eligibility != PoolPreferred {
		t.Error("Pure context should make pool preferred")
	}

	if !ctx.IsPoolPreferred("p") {
		t.Error("IsPoolPreferred should return true in pure context")
	}
}

func TestPoolContext_ScopeTracking(t *testing.T) {
	ctx := NewPoolContext()

	if ctx.ScopeDepth != 0 {
		t.Error("Initial scope depth should be 0")
	}

	ctx.EnterScope()
	if ctx.ScopeDepth != 1 {
		t.Error("Scope depth should be 1 after enter")
	}

	ctx.EnterScope()
	if ctx.ScopeDepth != 2 {
		t.Error("Scope depth should be 2 after second enter")
	}

	ctx.ExitScope()
	if ctx.ScopeDepth != 1 {
		t.Error("Scope depth should be 1 after exit")
	}

	ctx.ExitScope()
	if ctx.ScopeDepth != 0 {
		t.Error("Scope depth should be 0 after second exit")
	}
}

func TestPoolContext_Stats(t *testing.T) {
	ctx := NewPoolContext()

	// Add eligible variables
	ctx.EscapeCtx.AddVar("a")
	ctx.EscapeCtx.AddVar("b")
	ctx.AnalyzePoolEligibility("a", "int", nil)
	ctx.AnalyzePoolEligibility("b", "pair", nil)

	// Add ineligible variable
	ctx.EscapeCtx.AddVar("c")
	varUsage := ctx.EscapeCtx.FindVar("c")
	if varUsage != nil {
		varUsage.Escape = EscapeGlobal
	}
	ctx.AnalyzePoolEligibility("c", "box", nil)

	stats := ctx.GetPoolStats()
	if stats.Eligible != 2 {
		t.Errorf("Expected 2 eligible, got %d", stats.Eligible)
	}
	if stats.Ineligible != 1 {
		t.Errorf("Expected 1 ineligible, got %d", stats.Ineligible)
	}
}

func TestPoolEligibilityString(t *testing.T) {
	tests := []struct {
		e        PoolEligibility
		expected string
	}{
		{PoolIneligible, "ineligible"},
		{PoolEligible, "eligible"},
		{PoolPreferred, "preferred"},
	}

	for _, tc := range tests {
		got := tc.e.String()
		if got != tc.expected {
			t.Errorf("PoolEligibility(%d).String() = %s, want %s", tc.e, got, tc.expected)
		}
	}
}

func TestPoolContext_CountPoolEligible(t *testing.T) {
	ctx := NewPoolContext()

	ctx.EscapeCtx.AddVar("x")
	ctx.EscapeCtx.AddVar("y")
	ctx.EscapeCtx.AddVar("z")

	ctx.AnalyzePoolEligibility("x", "int", nil)
	ctx.AnalyzePoolEligibility("y", "int", nil)

	// Make z escape
	varUsage := ctx.EscapeCtx.FindVar("z")
	if varUsage != nil {
		varUsage.Escape = EscapeArg
	}
	ctx.AnalyzePoolEligibility("z", "int", nil)

	count := ctx.CountPoolEligible()
	if count != 2 {
		t.Errorf("Expected 2 pool-eligible, got %d", count)
	}
}
