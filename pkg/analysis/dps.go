package analysis

import "purple_go/pkg/ast"

// DPSCandidate represents a function eligible for DPS transformation.
type DPSCandidate struct {
	Name       string
	Params     []string
	ReturnType string
	IsTailCall bool
	BodyExpr   *ast.Value
}

// DPSAnalyzer identifies DPS transformation opportunities.
type DPSAnalyzer struct {
	Candidates map[string]*DPSCandidate
	Registry   *SummaryRegistry
}

// NewDPSAnalyzer creates a new DPS analyzer.
func NewDPSAnalyzer(registry *SummaryRegistry) *DPSAnalyzer {
	return &DPSAnalyzer{
		Candidates: make(map[string]*DPSCandidate),
		Registry:   registry,
	}
}

// AnalyzeFunction checks if a function is a DPS candidate.
func (da *DPSAnalyzer) AnalyzeFunction(name string, params *ast.Value, body *ast.Value) *DPSCandidate {
	summary := da.Registry.Lookup(name)
	if summary == nil {
		return nil
	}

	// Must return fresh allocation.
	if summary.Return == nil || !summary.Return.IsFresh {
		return nil
	}

	// Must allocate O(n) or more.
	if summary.Allocations == 0 {
		return nil
	}

	// Check if tail-recursive.
	isTail := da.isTailRecursive(body, name)

	candidate := &DPSCandidate{
		Name:       name,
		Params:     dpsExtractParamNames(params),
		ReturnType: "Obj",
		IsTailCall: isTail,
		BodyExpr:   body,
	}

	da.Candidates[name] = candidate
	return candidate
}

func dpsExtractParamNames(params *ast.Value) []string {
	var names []string
	for !ast.IsNil(params) && ast.IsCell(params) {
		param := params.Car
		if ast.IsSym(param) {
			names = append(names, param.Str)
		}
		params = params.Cdr
	}
	return names
}

func (da *DPSAnalyzer) isTailRecursive(body *ast.Value, fnName string) bool {
	return da.isInTailPosition(body, fnName, true)
}

func (da *DPSAnalyzer) isInTailPosition(expr *ast.Value, fnName string, isTail bool) bool {
	if expr == nil || ast.IsNil(expr) {
		return false
	}

	if !ast.IsCell(expr) {
		return false
	}

	if ast.IsSym(expr.Car) {
		switch expr.Car.Str {
		case fnName:
			return isTail
		case "if":
			if expr.Cdr == nil || expr.Cdr.Cdr == nil || expr.Cdr.Cdr.Cdr == nil {
				return false
			}
			thenBranch := expr.Cdr.Cdr.Car
			elseBranch := expr.Cdr.Cdr.Cdr.Car
			return da.isInTailPosition(thenBranch, fnName, isTail) ||
				da.isInTailPosition(elseBranch, fnName, isTail)
		case "let", "letrec":
			if expr.Cdr == nil || expr.Cdr.Cdr == nil {
				return false
			}
			bodyExpr := expr.Cdr.Cdr.Car
			return da.isInTailPosition(bodyExpr, fnName, isTail)
		case "do":
			last := expr.Cdr
			for last != nil && ast.IsCell(last.Cdr) && !ast.IsNil(last.Cdr) {
				last = last.Cdr
			}
			if last == nil || ast.IsNil(last) {
				return false
			}
			return da.isInTailPosition(last.Car, fnName, isTail)
		}
	}

	return false
}
