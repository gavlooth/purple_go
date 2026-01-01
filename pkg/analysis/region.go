package analysis

import "purple_go/pkg/ast"

// Region represents a memory region.
type Region struct {
	ID          int
	Name        string
	Parent      *Region
	Children    []*Region
	Allocations []string
	Lifetime    Lifetime
}

// Lifetime describes when a region can be freed.
type Lifetime int

const (
	LifetimeScope Lifetime = iota
	LifetimeReturn
	LifetimeGlobal
)

// RegionAnalyzer infers regions for allocations.
type RegionAnalyzer struct {
	Regions    map[int]*Region
	VarRegion  map[string]int
	NextID     int
	CurrentRgn *Region
}

// NewRegionAnalyzer creates a new region analyzer.
func NewRegionAnalyzer() *RegionAnalyzer {
	ra := &RegionAnalyzer{
		Regions:   make(map[int]*Region),
		VarRegion: make(map[string]int),
	}
	global := &Region{ID: 0, Name: "global", Lifetime: LifetimeGlobal}
	ra.Regions[0] = global
	ra.CurrentRgn = global
	ra.NextID = 1
	return ra
}

// EnterScope creates a new region for a scope.
func (ra *RegionAnalyzer) EnterScope(name string) *Region {
	rgn := &Region{
		ID:       ra.NextID,
		Name:     name,
		Parent:   ra.CurrentRgn,
		Lifetime: LifetimeScope,
	}
	ra.NextID++
	ra.Regions[rgn.ID] = rgn
	ra.CurrentRgn.Children = append(ra.CurrentRgn.Children, rgn)
	ra.CurrentRgn = rgn
	return rgn
}

// ExitScope returns to parent region.
func (ra *RegionAnalyzer) ExitScope() *Region {
	exited := ra.CurrentRgn
	if ra.CurrentRgn.Parent != nil {
		ra.CurrentRgn = ra.CurrentRgn.Parent
	}
	return exited
}

// AllocateIn allocates a variable in the current region.
func (ra *RegionAnalyzer) AllocateIn(varName string) {
	ra.CurrentRgn.Allocations = append(ra.CurrentRgn.Allocations, varName)
	ra.VarRegion[varName] = ra.CurrentRgn.ID
}

// Analyze performs region inference on an expression.
func (ra *RegionAnalyzer) Analyze(expr *ast.Value) {
	ra.analyzeExpr(expr)
}

func (ra *RegionAnalyzer) analyzeExpr(expr *ast.Value) {
	if expr == nil || ast.IsNil(expr) {
		return
	}
	if !ast.IsCell(expr) {
		return
	}

	if ast.IsSym(expr.Car) {
		switch expr.Car.Str {
		case "let", "letrec":
			ra.analyzeLet(expr)
		case "lambda":
			ra.analyzeLambda(expr)
		default:
			for e := expr.Cdr; !ast.IsNil(e) && ast.IsCell(e); e = e.Cdr {
				ra.analyzeExpr(e.Car)
			}
		}
	}
}

func (ra *RegionAnalyzer) analyzeLet(expr *ast.Value) {
	ra.EnterScope("let")

	bindings := expr.Cdr.Car
	for !ast.IsNil(bindings) && ast.IsCell(bindings) {
		binding := bindings.Car
		if ast.IsCell(binding) && ast.IsSym(binding.Car) {
			varName := binding.Car.Str
			ra.AllocateIn(varName)

			if binding.Cdr != nil && ast.IsCell(binding.Cdr) {
				ra.analyzeExpr(binding.Cdr.Car)
			}
		}
		bindings = bindings.Cdr
	}

	if expr.Cdr.Cdr != nil && ast.IsCell(expr.Cdr.Cdr) {
		ra.analyzeExpr(expr.Cdr.Cdr.Car)
	}

	ra.ExitScope()
}

func (ra *RegionAnalyzer) analyzeLambda(expr *ast.Value) {
	ra.EnterScope("lambda")
	if expr.Cdr != nil && expr.Cdr.Cdr != nil {
		ra.analyzeExpr(expr.Cdr.Cdr.Car)
	}
	ra.ExitScope()
}

// GetRegionForVar returns the region a variable is allocated in.
func (ra *RegionAnalyzer) GetRegionForVar(varName string) *Region {
	if id, ok := ra.VarRegion[varName]; ok {
		return ra.Regions[id]
	}
	return nil
}

// CanMergeRegions checks if two regions can be merged.
func (ra *RegionAnalyzer) CanMergeRegions(r1, r2 *Region) bool {
	if r1 == nil || r2 == nil {
		return false
	}
	if r1.Lifetime != r2.Lifetime {
		return false
	}
	return r1.Parent == r2 || r2.Parent == r1
}
