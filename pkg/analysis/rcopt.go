package analysis

import (
	"fmt"
	"purple_go/pkg/ast"
)

// RCOptimization represents the type of RC optimization applied
type RCOptimization int

const (
	RCOptNone        RCOptimization = iota
	RCOptElideIncRef                // Skip inc_ref (borrowed or already counted)
	RCOptElideDecRef                // Skip dec_ref (another alias will handle it)
	RCOptDirectFree                 // Use free() directly (proven unique)
	RCOptBatchedFree                // Batch multiple frees together
	RCOptElideAll                   // Eliminate all RC ops (unique + owned)
)

// RCOptInfo holds optimization information for a variable
type RCOptInfo struct {
	VarName       string
	Optimizations []RCOptimization
	Aliases       []string // Other variables that alias this one
	IsUnique      bool     // True if provably the only reference
	IsBorrowed    bool     // True if borrowed (no ownership transfer)
	IsConsumed    bool     // True if ownership was transferred to callee
	DefinedAt     int      // Program point where defined
	LastUsedAt    int      // Program point of last use
	AliasOf       string   // If this is an alias, name of original
	RefCountDelta int      // Net change to ref count (+1, 0, -1)
}

// RCStats tracks RC optimization statistics.
type RCStats struct {
	TotalOps      int
	EliminatedOps int
	UniqueSkips   int
	BorrowSkips   int
	TransferSkips int
}

// BorrowedRef tracks a borrowed reference relationship.
type BorrowedRef struct {
	Source     string // Variable borrowed from
	Field      string // Field accessed (empty for whole value)
	ValidUntil string // Scope where borrow ends
}

// RCOptContext holds RC optimization analysis state
type RCOptContext struct {
	Vars         map[string]*RCOptInfo
	AliasGroups  map[int][]string // Group ID -> list of aliased variables
	NextGroupID  int
	CurrentPoint int
	Eliminated   int // Count of eliminated RC operations
	Borrows      map[string]*BorrowedRef
	Stats        RCStats
}

// NewRCOptContext creates a new RC optimization context
func NewRCOptContext() *RCOptContext {
	return &RCOptContext{
		Vars:        make(map[string]*RCOptInfo),
		AliasGroups: make(map[int][]string),
		NextGroupID: 1,
		Borrows:     make(map[string]*BorrowedRef),
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
		IsUnique:      true, // Fresh allocations are unique
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

// DefineVarNonUnique defines a new variable that is not proven unique.
func (ctx *RCOptContext) DefineVarNonUnique(name string) *RCOptInfo {
	info := &RCOptInfo{
		VarName:       name,
		IsUnique:      false,
		IsBorrowed:    false,
		DefinedAt:     ctx.nextPoint(),
		RefCountDelta: 0,
	}
	ctx.Vars[name] = info
	return info
}

// MarkUnique marks a variable as unique.
func (ctx *RCOptContext) MarkUnique(name string) {
	if info := ctx.Vars[name]; info != nil {
		info.IsUnique = true
		return
	}
	ctx.DefineVar(name)
}

// IsUnique returns true if a variable is known unique.
func (ctx *RCOptContext) IsUnique(name string) bool {
	if info := ctx.Vars[name]; info != nil {
		return info.IsUnique
	}
	return false
}

// MarkBorrowed marks a variable as borrowed from another.
func (ctx *RCOptContext) MarkBorrowed(borrowed, source, field string) {
	ctx.Borrows[borrowed] = &BorrowedRef{
		Source: source,
		Field:  field,
	}
	ctx.DefineBorrowed(borrowed)
}

// IsBorrowed returns true if a variable is borrowed.
func (ctx *RCOptContext) IsBorrowed(name string) bool {
	if _, ok := ctx.Borrows[name]; ok {
		return true
	}
	if info := ctx.Vars[name]; info != nil {
		return info.IsBorrowed
	}
	return false
}

// MarkConsumed marks a variable as consumed (ownership transferred to callee).
// This means the caller should NOT dec_ref after the call - callee owns it now.
func (ctx *RCOptContext) MarkConsumed(name string) {
	if info := ctx.Vars[name]; info != nil {
		info.IsConsumed = true
		// Consumed variables don't need dec_ref - callee will free
		info.Optimizations = append(info.Optimizations, RCOptElideDecRef)
		ctx.Stats.TransferSkips++
	}
}

// IsConsumed returns true if a variable's ownership was transferred.
func (ctx *RCOptContext) IsConsumed(name string) bool {
	if info := ctx.Vars[name]; info != nil {
		return info.IsConsumed
	}
	return false
}

// TransferUniqueness transfers uniqueness from source to dest.
func (ctx *RCOptContext) TransferUniqueness(source, dest string) {
	if info := ctx.Vars[source]; info != nil && info.IsUnique {
		info.IsUnique = false
		if destInfo := ctx.Vars[dest]; destInfo != nil {
			destInfo.IsUnique = true
		} else {
			ctx.DefineVar(dest)
		}
	}
}

// MarkUsed marks a variable as used at the current program point
func (ctx *RCOptContext) MarkUsed(name string) {
	if info := ctx.Vars[name]; info != nil {
		info.LastUsedAt = ctx.nextPoint()
	}
}

// PropagateUniqueness propagates uniqueness through expressions.
func (ctx *RCOptContext) PropagateUniqueness(expr *ast.Value) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	switch expr.Tag {
	case ast.TCell:
		if ast.IsSym(expr.Car) {
			switch expr.Car.Str {
			case "let":
				ctx.propagateLetUniqueness(expr)
			case "if":
				ctx.propagateIfUniqueness(expr)
			case "lambda":
				ctx.propagateLambdaUniqueness(expr)
			}
		}
	}
}

func (ctx *RCOptContext) propagateLetUniqueness(expr *ast.Value) {
	bindings := expr.Cdr.Car
	for !ast.IsNil(bindings) && ast.IsCell(bindings) {
		binding := bindings.Car
		if ast.IsCell(binding) && ast.IsSym(binding.Car) {
			varName := binding.Car.Str
			initExpr := binding.Cdr.Car

			if ctx.isFreshAllocation(initExpr) {
				ctx.MarkUnique(varName)
			} else if ast.IsSym(initExpr) {
				sourceVar := initExpr.Str
				if ctx.IsUnique(sourceVar) {
					ctx.TransferUniqueness(sourceVar, varName)
				}
			}
		}
		bindings = bindings.Cdr
	}
}

func (ctx *RCOptContext) propagateIfUniqueness(expr *ast.Value) {
	// Recurse into branches to capture local uniqueness transfers.
	thenBranch := expr.Cdr.Cdr.Car
	elseBranch := expr.Cdr.Cdr.Cdr.Car
	ctx.PropagateUniqueness(thenBranch)
	ctx.PropagateUniqueness(elseBranch)
}

func (ctx *RCOptContext) propagateLambdaUniqueness(expr *ast.Value) {
	// Lambda parameters are borrowed.
	params := expr.Cdr.Car
	for !ast.IsNil(params) && ast.IsCell(params) {
		param := params.Car
		if ast.IsSym(param) {
			ctx.DefineBorrowed(param.Str)
		}
		params = params.Cdr
	}
	body := expr.Cdr.Cdr.Car
	ctx.PropagateUniqueness(body)
}

func (ctx *RCOptContext) isFreshAllocation(expr *ast.Value) bool {
	if !ast.IsCell(expr) {
		return false
	}
	if ast.IsSym(expr.Car) {
		switch expr.Car.Str {
		case "cons", "list", "box", "mk-int", "mk-pair", "lambda":
			return true
		}
		if len(expr.Car.Str) > 3 && expr.Car.Str[:3] == "mk-" {
			return true
		}
	}
	return false
}

// IsFreshAllocation reports whether an expression is a fresh allocation.
func (ctx *RCOptContext) IsFreshAllocation(expr *ast.Value) bool {
	return ctx.isFreshAllocation(expr)
}

func (ctx *RCOptContext) recordOp(eliminated bool) {
	ctx.Stats.TotalOps++
	if eliminated {
		ctx.Stats.EliminatedOps++
		ctx.Eliminated++
	}
}

func (ctx *RCOptContext) recordSkip(kind string) {
	ctx.recordOp(true)
	switch kind {
	case "unique":
		ctx.Stats.UniqueSkips++
	case "borrow":
		ctx.Stats.BorrowSkips++
	case "transfer":
		ctx.Stats.TransferSkips++
	}
}

// RecordTransferSkip records an RC elimination due to ownership transfer.
func (ctx *RCOptContext) RecordTransferSkip() {
	ctx.recordSkip("transfer")
}

// GetOptimizedIncRef returns the optimized inc_ref strategy
func (ctx *RCOptContext) GetOptimizedIncRef(name string) RCOptimization {
	info := ctx.Vars[name]
	if info == nil {
		ctx.recordOp(false)
		return RCOptNone
	}

	// Borrowed references don't need inc_ref
	if info.IsBorrowed {
		ctx.recordSkip("borrow")
		return RCOptElideIncRef
	}

	// If this is an alias and original will handle RC, skip
	if info.AliasOf != "" {
		original := ctx.Vars[info.AliasOf]
		if original != nil && !original.IsBorrowed {
			// Let the original handle the RC
			ctx.recordOp(true)
			return RCOptElideIncRef
		}
	}

	ctx.recordOp(false)
	return RCOptNone
}

// GetOptimizedDecRef returns the optimized dec_ref strategy
func (ctx *RCOptContext) GetOptimizedDecRef(name string) RCOptimization {
	info := ctx.Vars[name]
	if info == nil {
		ctx.recordOp(false)
		return RCOptNone
	}

	// Consumed references don't need dec_ref - callee owns them now
	if info.IsConsumed {
		ctx.recordSkip("transfer")
		return RCOptElideDecRef
	}

	// Borrowed references don't need dec_ref
	if info.IsBorrowed {
		ctx.recordSkip("borrow")
		return RCOptElideDecRef
	}

	// If this is an alias, check if another alias will dec_ref
	if len(info.Aliases) > 0 {
		for _, alias := range info.Aliases {
			aliasInfo := ctx.Vars[alias]
			if aliasInfo != nil && aliasInfo.LastUsedAt > info.LastUsedAt {
				// Another alias is used later, it will handle dec_ref
				ctx.recordOp(true)
				return RCOptElideDecRef
			}
		}
	}

	// If proven unique, can use direct free
	if info.IsUnique {
		ctx.recordSkip("unique")
		return RCOptDirectFree
	}

	ctx.recordOp(false)
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

// Analyze performs RC analysis and uniqueness propagation.
func (ctx *RCOptContext) Analyze(expr *ast.Value) {
	ctx.AnalyzeExpr(expr)
	ctx.PropagateUniqueness(expr)
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
			if ast.IsSym(valExpr) {
				if ctx.IsUnique(valExpr.Str) {
					ctx.TransferUniqueness(valExpr.Str, sym.Str)
				} else {
					ctx.DefineAlias(sym.Str, valExpr.Str)
				}
			} else if ctx.isFreshAllocation(valExpr) {
				ctx.DefineVar(sym.Str)
			} else {
				ctx.DefineVarNonUnique(sym.Str)
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

// GetStats returns RC optimization statistics.
func (ctx *RCOptContext) GetStats() RCStats {
	return ctx.Stats
}

// ReportStats returns a formatted stats summary.
func (ctx *RCOptContext) ReportStats() string {
	s := ctx.Stats
	pct := 0.0
	if s.TotalOps > 0 {
		pct = float64(s.EliminatedOps) / float64(s.TotalOps) * 100
	}
	return fmt.Sprintf("RC ops: %d total, %d eliminated (%.1f%%) [unique:%d, borrow:%d, transfer:%d]",
		s.TotalOps, s.EliminatedOps, pct,
		s.UniqueSkips, s.BorrowSkips, s.TransferSkips)
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

// GetElisionReason returns a human-readable reason for why an RC op was elided
func (ctx *RCOptContext) GetElisionReason(name string, op string) string {
	info := ctx.Vars[name]
	if info == nil {
		return "unknown"
	}

	if op == "inc" {
		opt := ctx.GetOptimizedIncRef(name)
		switch opt {
		case RCOptElideIncRef:
			if info.IsBorrowed {
				return "borrowed"
			}
			return "already counted"
		case RCOptElideAll:
			return "unique+owned"
		}
	} else if op == "dec" {
		if info.IsConsumed {
			return "consumed by callee"
		}
		if info.IsBorrowed {
			return "borrowed"
		}
		if info.IsUnique {
			return "unique (use free_unique)"
		}
		if len(info.Aliases) > 0 {
			return "alias will handle"
		}
	}
	return "optimized"
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
