package analysis

import (
	"purple_go/pkg/ast"
)

// RCOptimization represents the type of RC optimization applied
type RCOptimization int

const (
	RCOptNone          RCOptimization = iota
	RCOptElideIncRef                  // Skip inc_ref (borrowed or already counted)
	RCOptElideDecRef                  // Skip dec_ref (another alias will handle it)
	RCOptDirectFree                   // Use free() directly (proven unique)
	RCOptBatchedFree                  // Batch multiple frees together
	RCOptElideAll                     // Eliminate all RC ops (unique + owned)
)

// RCOptInfo holds optimization information for a variable
type RCOptInfo struct {
	VarName       string
	Optimizations []RCOptimization
	Aliases       []string   // Other variables that alias this one
	IsUnique      bool       // True if provably the only reference
	IsBorrowed    bool       // True if borrowed (no ownership transfer)
	DefinedAt     int        // Program point where defined
	LastUsedAt    int        // Program point of last use
	AliasOf       string     // If this is an alias, name of original
	RefCountDelta int        // Net change to ref count (+1, 0, -1)
}

// RCOptContext holds RC optimization analysis state
type RCOptContext struct {
	Vars         map[string]*RCOptInfo
	AliasGroups  map[int][]string // Group ID -> list of aliased variables
	NextGroupID  int
	CurrentPoint int
	Eliminated   int // Count of eliminated RC operations
}

// NewRCOptContext creates a new RC optimization context
func NewRCOptContext() *RCOptContext {
	return &RCOptContext{
		Vars:        make(map[string]*RCOptInfo),
		AliasGroups: make(map[int][]string),
		NextGroupID: 1,
	}
}

// nextPoint advances the program point
func (ctx *RCOptContext) nextPoint() int {
	ctx.CurrentPoint++
	return ctx.CurrentPoint
}

// DefineVar defines a new variable from a fresh allocation (unique)
func (ctx *RCOptContext) DefineVar(name string) *RCOptInfo {
	info := &RCOptInfo{
		VarName:       name,
		IsUnique:      true,  // Fresh allocations are unique
		IsBorrowed:    false,
		DefinedAt:     ctx.nextPoint(),
		RefCountDelta: 0,
	}
	ctx.Vars[name] = info
	return info
}

// DefineAlias defines a variable as an alias of another
func (ctx *RCOptContext) DefineAlias(name, aliasOf string) *RCOptInfo {
	original := ctx.Vars[aliasOf]
	if original == nil {
		// Unknown original, be conservative
		return ctx.DefineVar(name)
	}

	// The original is no longer unique
	original.IsUnique = false

	info := &RCOptInfo{
		VarName:       name,
		IsUnique:      false,
		IsBorrowed:    false,
		DefinedAt:     ctx.nextPoint(),
		AliasOf:       aliasOf,
		RefCountDelta: 0,
	}

	// Add to each other's alias list
	original.Aliases = append(original.Aliases, name)
	info.Aliases = append(info.Aliases, aliasOf)

	ctx.Vars[name] = info
	return info
}

// DefineBorrowed defines a borrowed reference (no RC ops needed)
func (ctx *RCOptContext) DefineBorrowed(name string) *RCOptInfo {
	info := &RCOptInfo{
		VarName:       name,
		IsUnique:      false,
		IsBorrowed:    true,
		DefinedAt:     ctx.nextPoint(),
		RefCountDelta: 0,
	}
	ctx.Vars[name] = info
	return info
}

// MarkUsed marks a variable as used at the current program point
func (ctx *RCOptContext) MarkUsed(name string) {
	if info := ctx.Vars[name]; info != nil {
		info.LastUsedAt = ctx.nextPoint()
	}
}

// GetOptimizedIncRef returns the optimized inc_ref strategy
func (ctx *RCOptContext) GetOptimizedIncRef(name string) RCOptimization {
	info := ctx.Vars[name]
	if info == nil {
		return RCOptNone
	}

	// Borrowed references don't need inc_ref
	if info.IsBorrowed {
		ctx.Eliminated++
		return RCOptElideIncRef
	}

	// If this is an alias and original will handle RC, skip
	if info.AliasOf != "" {
		original := ctx.Vars[info.AliasOf]
		if original != nil && !original.IsBorrowed {
			// Let the original handle the RC
			ctx.Eliminated++
			return RCOptElideIncRef
		}
	}

	return RCOptNone
}

// GetOptimizedDecRef returns the optimized dec_ref strategy
func (ctx *RCOptContext) GetOptimizedDecRef(name string) RCOptimization {
	info := ctx.Vars[name]
	if info == nil {
		return RCOptNone
	}

	// Borrowed references don't need dec_ref
	if info.IsBorrowed {
		ctx.Eliminated++
		return RCOptElideDecRef
	}

	// If this is an alias, check if another alias will dec_ref
	if len(info.Aliases) > 0 {
		for _, alias := range info.Aliases {
			aliasInfo := ctx.Vars[alias]
			if aliasInfo != nil && aliasInfo.LastUsedAt > info.LastUsedAt {
				// Another alias is used later, it will handle dec_ref
				ctx.Eliminated++
				return RCOptElideDecRef
			}
		}
	}

	// If proven unique, can use direct free
	if info.IsUnique {
		ctx.Eliminated++
		return RCOptDirectFree
	}

	return RCOptNone
}

// AnalyzeExpr analyzes an expression for RC optimization opportunities
func (ctx *RCOptContext) AnalyzeExpr(expr *ast.Value) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	switch expr.Tag {
	case ast.TInt, ast.TNil, ast.TFloat, ast.TChar:
		// Literals - no RC tracking needed

	case ast.TSym:
		// Variable use
		ctx.MarkUsed(expr.Str)

	case ast.TCell:
		op := expr.Car
		args := expr.Cdr

		if ast.IsSym(op) {
			switch op.Str {
			case "let":
				ctx.analyzeLetRC(args)
				return

			case "set!":
				ctx.analyzeSetRC(args)
				return

			case "lambda":
				ctx.analyzeLambdaRC(args)
				return
			}
		}

		// Default: analyze all subexpressions
		ctx.AnalyzeExpr(op)
		for !ast.IsNil(args) && ast.IsCell(args) {
			ctx.AnalyzeExpr(args.Car)
			args = args.Cdr
		}
	}
}

func (ctx *RCOptContext) analyzeLetRC(args *ast.Value) {
	if ast.IsNil(args) {
		return
	}

	bindings := args.Car
	body := args.Cdr.Car

	// Process bindings
	for !ast.IsNil(bindings) && ast.IsCell(bindings) {
		bind := bindings.Car
		sym := bind.Car
		valExpr := bind.Cdr.Car

		// Analyze the value expression first
		ctx.AnalyzeExpr(valExpr)

		// Define the bound variable
		if ast.IsSym(sym) {
			// Check if value is a simple variable (creates alias)
			if ast.IsSym(valExpr) {
				ctx.DefineAlias(sym.Str, valExpr.Str)
			} else {
				// Fresh allocation
				ctx.DefineVar(sym.Str)
			}
		}

		bindings = bindings.Cdr
	}

	// Analyze body
	ctx.AnalyzeExpr(body)
}

func (ctx *RCOptContext) analyzeSetRC(args *ast.Value) {
	if ast.IsNil(args) {
		return
	}

	target := args.Car
	value := args.Cdr.Car

	ctx.AnalyzeExpr(value)

	// set! creates an alias
	if ast.IsSym(target) && ast.IsSym(value) {
		ctx.DefineAlias(target.Str, value.Str)
	}
}

func (ctx *RCOptContext) analyzeLambdaRC(args *ast.Value) {
	if ast.IsNil(args) {
		return
	}

	params := args.Car
	body := args.Cdr.Car

	// Parameters are borrowed
	for !ast.IsNil(params) && ast.IsCell(params) {
		param := params.Car
		if ast.IsSym(param) {
			ctx.DefineBorrowed(param.Str)
		}
		params = params.Cdr
	}

	// Analyze body
	ctx.AnalyzeExpr(body)
}

// GetStatistics returns optimization statistics
func (ctx *RCOptContext) GetStatistics() (total, eliminated int) {
	total = len(ctx.Vars) * 2 // Assume 1 inc_ref + 1 dec_ref per var
	eliminated = ctx.Eliminated
	return
}

// ShouldEmitIncRef returns whether inc_ref should be emitted for a variable
func (ctx *RCOptContext) ShouldEmitIncRef(name string) bool {
	return ctx.GetOptimizedIncRef(name) == RCOptNone
}

// ShouldEmitDecRef returns whether dec_ref should be emitted for a variable
func (ctx *RCOptContext) ShouldEmitDecRef(name string) bool {
	opt := ctx.GetOptimizedDecRef(name)
	return opt == RCOptNone || opt == RCOptDirectFree
}

// GetFreeFunction returns the best free function to use
func (ctx *RCOptContext) GetFreeFunction(name string, shape Shape) string {
	opt := ctx.GetOptimizedDecRef(name)

	switch opt {
	case RCOptDirectFree:
		// Proven unique - can free directly without RC check
		return "free_unique"
	case RCOptElideDecRef:
		// Skip the dec_ref entirely
		return ""
	default:
		// Use shape-based strategy
		return ShapeFreeStrategy(shape)
	}
}

// RCOptString returns string representation of RC optimization
func RCOptString(opt RCOptimization) string {
	switch opt {
	case RCOptElideIncRef:
		return "elide_inc_ref"
	case RCOptElideDecRef:
		return "elide_dec_ref"
	case RCOptDirectFree:
		return "direct_free"
	case RCOptBatchedFree:
		return "batched_free"
	case RCOptElideAll:
		return "elide_all"
	default:
		return "none"
	}
}
