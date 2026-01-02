package analysis

import (
	"purple_go/pkg/ast"
)

// PoolEligibility represents whether a value can be pool-allocated
type PoolEligibility int

const (
	PoolIneligible PoolEligibility = iota // Must use heap
	PoolEligible                          // Can use pool allocation
	PoolPreferred                         // Strongly benefits from pool
)

func (p PoolEligibility) String() string {
	switch p {
	case PoolEligible:
		return "eligible"
	case PoolPreferred:
		return "preferred"
	default:
		return "ineligible"
	}
}

// PoolCandidate represents a variable that could use pool allocation
type PoolCandidate struct {
	VarName     string
	TypeName    string
	Eligibility PoolEligibility
	Reason      string
}

// PoolContext tracks pool allocation eligibility
type PoolContext struct {
	EscapeCtx    *AnalysisContext
	PurityCtx    *PurityContext
	OwnershipCtx *OwnershipContext
	Candidates   map[string]*PoolCandidate
	ScopeDepth   int
}

// NewPoolContext creates a new pool analysis context
func NewPoolContext() *PoolContext {
	return &PoolContext{
		EscapeCtx:  NewAnalysisContext(),
		PurityCtx:  NewPurityContext(),
		Candidates: make(map[string]*PoolCandidate),
	}
}

// SetOwnershipContext sets the ownership context for pool analysis
func (ctx *PoolContext) SetOwnershipContext(ownerCtx *OwnershipContext) {
	ctx.OwnershipCtx = ownerCtx
}

// EnterScope enters a new scope
func (ctx *PoolContext) EnterScope() {
	ctx.ScopeDepth++
}

// ExitScope exits a scope
func (ctx *PoolContext) ExitScope() {
	if ctx.ScopeDepth > 0 {
		ctx.ScopeDepth--
	}
}

// AnalyzePoolEligibility determines if a variable can use pool allocation
func (ctx *PoolContext) AnalyzePoolEligibility(varName, typeName string, initExpr *ast.Value) PoolEligibility {
	// Check escape class - only non-escaping values can be pooled
	varUsage := ctx.EscapeCtx.FindVar(varName)
	if varUsage != nil {
		if varUsage.Escape == EscapeGlobal || varUsage.Escape == EscapeArg {
			ctx.Candidates[varName] = &PoolCandidate{
				VarName:     varName,
				TypeName:    typeName,
				Eligibility: PoolIneligible,
				Reason:      "escapes scope",
			}
			return PoolIneligible
		}

		// Check if captured by closure
		if varUsage.CapturedByLambda {
			ctx.Candidates[varName] = &PoolCandidate{
				VarName:     varName,
				TypeName:    typeName,
				Eligibility: PoolIneligible,
				Reason:      "captured by closure",
			}
			return PoolIneligible
		}
	}

	// Check if in pure context (strongly prefer pool)
	isPure := ctx.PurityCtx != nil && ctx.PurityCtx.IsPureContext()

	// Check ownership - borrowed and consumed skip pool
	if ctx.OwnershipCtx != nil {
		mode := ctx.OwnershipCtx.GetOwnershipMode(varName)
		if mode == ModeBorrowed {
			ctx.Candidates[varName] = &PoolCandidate{
				VarName:     varName,
				TypeName:    typeName,
				Eligibility: PoolIneligible,
				Reason:      "borrowed reference",
			}
			return PoolIneligible
		}
	}

	eligibility := PoolEligible
	reason := "non-escaping local"
	if isPure {
		eligibility = PoolPreferred
		reason = "pure context, non-escaping"
	}

	ctx.Candidates[varName] = &PoolCandidate{
		VarName:     varName,
		TypeName:    typeName,
		Eligibility: eligibility,
		Reason:      reason,
	}

	return eligibility
}

// IsPoolEligible returns true if a variable can use pool allocation
func (ctx *PoolContext) IsPoolEligible(varName string) bool {
	if c, ok := ctx.Candidates[varName]; ok {
		return c.Eligibility != PoolIneligible
	}
	return false
}

// IsPoolPreferred returns true if pool allocation is strongly recommended
func (ctx *PoolContext) IsPoolPreferred(varName string) bool {
	if c, ok := ctx.Candidates[varName]; ok {
		return c.Eligibility == PoolPreferred
	}
	return false
}

// GetPoolCandidate returns pool candidate info for a variable
func (ctx *PoolContext) GetPoolCandidate(varName string) *PoolCandidate {
	return ctx.Candidates[varName]
}

// CountPoolEligible returns the number of pool-eligible variables
func (ctx *PoolContext) CountPoolEligible() int {
	count := 0
	for _, c := range ctx.Candidates {
		if c.Eligibility != PoolIneligible {
			count++
		}
	}
	return count
}

// PoolStats tracks pool allocation statistics
type PoolStats struct {
	Eligible   int // Variables eligible for pool
	Preferred  int // Variables preferred for pool
	Ineligible int // Variables that must use heap
	PoolAllocs int // Actual pool allocations made
	HeapAllocs int // Heap allocations that could have been pooled
}

// GetPoolStats returns pool allocation statistics
func (ctx *PoolContext) GetPoolStats() PoolStats {
	stats := PoolStats{}
	for _, c := range ctx.Candidates {
		switch c.Eligibility {
		case PoolEligible:
			stats.Eligible++
		case PoolPreferred:
			stats.Preferred++
		case PoolIneligible:
			stats.Ineligible++
		}
	}
	return stats
}
