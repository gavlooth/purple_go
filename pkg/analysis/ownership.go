package analysis

import (
	"purple_go/pkg/ast"
)

// OwnershipClass represents the ownership classification of a value
type OwnershipClass int

const (
	OwnerUnknown     OwnershipClass = iota
	OwnerLocal                      // Owned by current scope - can free at scope exit
	OwnerBorrowed                   // Borrowed reference - do not free
	OwnerTransferred                // Ownership transferred to callee/field
	OwnerShared                     // Shared ownership - use reference counting
	OwnerWeak                       // Weak reference - do not count
	OwnerConsumed                   // Consumed by callee - callee takes ownership
)

// OwnershipInfo holds ownership information for a variable
type OwnershipInfo struct {
	VarName         string
	Class           OwnershipClass
	DefinedAt       int    // Program point where defined
	TransferredAt   int    // Program point where ownership transferred (-1 if not)
	TransferredTo   string // Name of variable/field that received ownership
	ConsumedAt      int    // Program point where consumed by callee (-1 if not)
	ConsumedBy      string // Function that consumed the value
	SourceField     string // If this came from a field access
	SourceFieldWeak bool   // True if source field is weak
	IsPure          bool   // True if used only in pure context (can skip RC)
	UseCount        int    // Number of times this variable is used
}

// OwnershipContext holds ownership analysis state
type OwnershipContext struct {
	Owners        map[string]*OwnershipInfo
	CurrentPoint  int
	ScopeStack    []string // Stack of scope names
	FieldRegistry FieldStrengthLookup
}

// FieldStrengthLookup is an interface to look up field strength
// This allows us to check if a field is weak without importing codegen
type FieldStrengthLookup interface {
	IsFieldWeak(typeName, fieldName string) bool
}

// NewOwnershipContext creates a new ownership analysis context
func NewOwnershipContext(fieldRegistry FieldStrengthLookup) *OwnershipContext {
	return &OwnershipContext{
		Owners:        make(map[string]*OwnershipInfo),
		CurrentPoint:  0,
		ScopeStack:    []string{"global"},
		FieldRegistry: fieldRegistry,
	}
}

// nextPoint advances and returns the next program point
func (ctx *OwnershipContext) nextPoint() int {
	ctx.CurrentPoint++
	return ctx.CurrentPoint
}

// EnterScope enters a new scope
func (ctx *OwnershipContext) EnterScope(name string) {
	ctx.ScopeStack = append(ctx.ScopeStack, name)
}

// ExitScope exits the current scope
func (ctx *OwnershipContext) ExitScope() {
	if len(ctx.ScopeStack) > 1 {
		ctx.ScopeStack = ctx.ScopeStack[:len(ctx.ScopeStack)-1]
	}
}

// CurrentScope returns the current scope name
func (ctx *OwnershipContext) CurrentScope() string {
	if len(ctx.ScopeStack) == 0 {
		return "global"
	}
	return ctx.ScopeStack[len(ctx.ScopeStack)-1]
}

// DefineOwned defines a new locally-owned variable
func (ctx *OwnershipContext) DefineOwned(name string) {
	ctx.Owners[name] = &OwnershipInfo{
		VarName:       name,
		Class:         OwnerLocal,
		DefinedAt:     ctx.nextPoint(),
		TransferredAt: -1,
		ConsumedAt:    -1,
	}
}

// DefineBorrowed defines a borrowed reference (e.g., function parameter)
func (ctx *OwnershipContext) DefineBorrowed(name string) {
	ctx.Owners[name] = &OwnershipInfo{
		VarName:       name,
		Class:         OwnerBorrowed,
		DefinedAt:     ctx.nextPoint(),
		TransferredAt: -1,
		ConsumedAt:    -1,
	}
}

// DefineConsumed defines a variable that will be consumed (ownership taken by callee)
func (ctx *OwnershipContext) DefineConsumed(name string, consumer string) {
	ctx.Owners[name] = &OwnershipInfo{
		VarName:       name,
		Class:         OwnerLocal, // Starts owned, will be consumed
		DefinedAt:     ctx.nextPoint(),
		TransferredAt: -1,
		ConsumedAt:    -1,
		ConsumedBy:    consumer, // Will be consumed by this function
	}
}

// DefineFromFieldAccess defines a variable from a field access
func (ctx *OwnershipContext) DefineFromFieldAccess(name, typeName, fieldName string) {
	isWeak := false
	if ctx.FieldRegistry != nil {
		isWeak = ctx.FieldRegistry.IsFieldWeak(typeName, fieldName)
	}

	class := OwnerLocal
	if isWeak {
		class = OwnerWeak
	}

	ctx.Owners[name] = &OwnershipInfo{
		VarName:         name,
		Class:           class,
		DefinedAt:       ctx.nextPoint(),
		TransferredAt:   -1,
		ConsumedAt:      -1,
		SourceField:     fieldName,
		SourceFieldWeak: isWeak,
	}
}

// TransferOwnership marks a variable's ownership as transferred
func (ctx *OwnershipContext) TransferOwnership(from, to string) {
	if info := ctx.Owners[from]; info != nil {
		if info.Class == OwnerLocal {
			info.Class = OwnerTransferred
			info.TransferredAt = ctx.nextPoint()
			info.TransferredTo = to
		}
	}
}

// ShareOwnership marks a variable as shared (used in multiple places)
func (ctx *OwnershipContext) ShareOwnership(name string) {
	if info := ctx.Owners[name]; info != nil {
		if info.Class == OwnerLocal {
			info.Class = OwnerShared
		}
	}
}

// GetOwnership returns ownership info for a variable
func (ctx *OwnershipContext) GetOwnership(name string) *OwnershipInfo {
	return ctx.Owners[name]
}

// ShouldFree returns true if the variable should be freed at scope exit
func (ctx *OwnershipContext) ShouldFree(name string) bool {
	info := ctx.Owners[name]
	if info == nil {
		return false
	}

	// Consumed values are freed by callee
	if info.ConsumedAt >= 0 {
		return false
	}

	switch info.Class {
	case OwnerLocal:
		return true // Locally owned, not transferred
	case OwnerShared:
		return true // Shared uses dec_ref
	case OwnerBorrowed, OwnerTransferred, OwnerWeak:
		return false // Not our responsibility
	default:
		return false
	}
}

// ConsumeOwnership marks a variable as consumed by a callee
// The callee takes ownership - caller should not inc_ref or dec_ref
func (ctx *OwnershipContext) ConsumeOwnership(name, consumer string) {
	if info := ctx.Owners[name]; info != nil {
		if info.Class == OwnerLocal || info.Class == OwnerShared {
			info.ConsumedAt = ctx.nextPoint()
			info.ConsumedBy = consumer
		}
	}
}

// IsConsumed returns true if the variable was consumed by a callee
func (ctx *OwnershipContext) IsConsumed(name string) bool {
	if info := ctx.Owners[name]; info != nil {
		return info.ConsumedAt >= 0
	}
	return false
}

// GetConsumer returns the function that consumed the variable
func (ctx *OwnershipContext) GetConsumer(name string) string {
	if info := ctx.Owners[name]; info != nil {
		return info.ConsumedBy
	}
	return ""
}

// NeedsIncRef returns true if the variable needs inc_ref before passing to callee
// Lobster-style: borrowed and consumed params skip inc_ref
func (ctx *OwnershipContext) NeedsIncRef(name string, paramOwnership OwnershipClass) bool {
	// Borrowed params: callee just reads, no RC needed
	if paramOwnership == OwnerBorrowed {
		return false
	}
	// Consumed params: ownership transfers, no inc_ref needed
	if paramOwnership == OwnerConsumed {
		return false
	}
	// Pure context: skip RC
	if info := ctx.Owners[name]; info != nil && info.IsPure {
		return false
	}
	// Shared params: need inc_ref so callee can keep reference
	return paramOwnership == OwnerShared
}

// NeedsDecRef returns true if the variable needs dec_ref at scope exit
// Lobster-style: consumed and transferred skip dec_ref
func (ctx *OwnershipContext) NeedsDecRef(name string) bool {
	info := ctx.Owners[name]
	if info == nil {
		return false
	}

	// Already consumed - callee will free
	if info.ConsumedAt >= 0 {
		return false
	}

	// Pure context - skip RC
	if info.IsPure {
		return false
	}

	switch info.Class {
	case OwnerLocal:
		return true
	case OwnerShared:
		return true
	case OwnerBorrowed, OwnerTransferred, OwnerWeak:
		return false
	default:
		return false
	}
}

// MarkAsPure marks a variable as used in pure context (skip RC)
func (ctx *OwnershipContext) MarkAsPure(name string) {
	if info := ctx.Owners[name]; info != nil {
		info.IsPure = true
	}
}

// IncrementUseCount increments the use count for a variable
func (ctx *OwnershipContext) IncrementUseCount(name string) {
	if info := ctx.Owners[name]; info != nil {
		info.UseCount++
	}
}

// GetUseCount returns the use count for a variable
func (ctx *OwnershipContext) GetUseCount(name string) int {
	if info := ctx.Owners[name]; info != nil {
		return info.UseCount
	}
	return 0
}

// IsSingleUse returns true if the variable is used exactly once
// Single-use values can skip RC (Lobster-style linear ownership)
func (ctx *OwnershipContext) IsSingleUse(name string) bool {
	if info := ctx.Owners[name]; info != nil {
		return info.UseCount == 1
	}
	return false
}

// OwnershipMode represents Lobster-style ownership modes for codegen
type OwnershipMode int

const (
	ModeOwned    OwnershipMode = iota // Caller owns, must free
	ModeBorrowed                      // Temporary access, no RC
	ModeConsumed                      // Ownership transfers to callee
)

// GetOwnershipMode returns the effective Lobster-style ownership mode
func (ctx *OwnershipContext) GetOwnershipMode(name string) OwnershipMode {
	info := ctx.Owners[name]
	if info == nil {
		return ModeOwned // Default to owned (safe)
	}

	// Already consumed
	if info.ConsumedAt >= 0 {
		return ModeConsumed
	}

	switch info.Class {
	case OwnerBorrowed:
		return ModeBorrowed
	case OwnerTransferred:
		return ModeConsumed
	case OwnerWeak:
		return ModeBorrowed // Weak is similar to borrowed for RC purposes
	default:
		return ModeOwned
	}
}

// OwnershipModeString returns string representation of ownership mode
func OwnershipModeString(m OwnershipMode) string {
	switch m {
	case ModeOwned:
		return "owned"
	case ModeBorrowed:
		return "borrowed"
	case ModeConsumed:
		return "consumed"
	default:
		return "unknown"
	}
}

// AnalyzeOwnership analyzes an expression for ownership
func (ctx *OwnershipContext) AnalyzeOwnership(expr *ast.Value) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	switch expr.Tag {
	case ast.TInt, ast.TNil, ast.TFloat, ast.TChar:
		// Literals - no ownership tracking needed

	case ast.TSym:
		// Variable reference - check if it's being used
		if info := ctx.Owners[expr.Str]; info != nil {
			// If used more than once, it might become shared
			// This is tracked by the escape analysis
		}

	case ast.TCell:
		op := expr.Car
		args := expr.Cdr

		if ast.IsSym(op) {
			switch op.Str {
			case "let":
				ctx.analyzeLetOwnership(args)
				return

			case "letrec":
				ctx.analyzeLetrecOwnership(args)
				return

			case "set!":
				ctx.analyzeSetOwnership(args)
				return

			case "lambda":
				ctx.analyzeLambdaOwnership(args)
				return

			case "cons", "mk_pair":
				// Constructor - args become owned by the new pair
				ctx.analyzeConstructorOwnership(args)
				return
			}
		}

		// Default: analyze all subexpressions
		ctx.AnalyzeOwnership(op)
		for !ast.IsNil(args) && ast.IsCell(args) {
			ctx.AnalyzeOwnership(args.Car)
			args = args.Cdr
		}
	}
}

func (ctx *OwnershipContext) analyzeLetOwnership(args *ast.Value) {
	if ast.IsNil(args) {
		return
	}

	bindings := args.Car
	body := args.Cdr.Car

	ctx.EnterScope("let")

	// Process bindings
	for !ast.IsNil(bindings) && ast.IsCell(bindings) {
		bind := bindings.Car
		sym := bind.Car
		valExpr := bind.Cdr.Car

		// Analyze the value expression
		ctx.AnalyzeOwnership(valExpr)

		// The bound variable owns the value
		if ast.IsSym(sym) {
			ctx.DefineOwned(sym.Str)
		}

		bindings = bindings.Cdr
	}

	// Analyze body
	ctx.AnalyzeOwnership(body)

	ctx.ExitScope()
}

func (ctx *OwnershipContext) analyzeLetrecOwnership(args *ast.Value) {
	if ast.IsNil(args) {
		return
	}

	bindings := args.Car
	body := args.Cdr.Car

	ctx.EnterScope("letrec")

	// Pre-define all bindings (they're mutually recursive)
	b := bindings
	for !ast.IsNil(b) && ast.IsCell(b) {
		bind := b.Car
		sym := bind.Car
		if ast.IsSym(sym) {
			// letrec bindings are shared (can reference each other)
			ctx.DefineOwned(sym.Str)
			ctx.ShareOwnership(sym.Str)
		}
		b = b.Cdr
	}

	// Analyze binding expressions
	b = bindings
	for !ast.IsNil(b) && ast.IsCell(b) {
		bind := b.Car
		valExpr := bind.Cdr.Car
		ctx.AnalyzeOwnership(valExpr)
		b = b.Cdr
	}

	// Analyze body
	ctx.AnalyzeOwnership(body)

	ctx.ExitScope()
}

func (ctx *OwnershipContext) analyzeSetOwnership(args *ast.Value) {
	if ast.IsNil(args) {
		return
	}

	target := args.Car
	value := args.Cdr.Car

	// Analyze the value being assigned
	ctx.AnalyzeOwnership(value)

	// If assigning to a variable, ownership transfers
	if ast.IsSym(target) {
		// If the value is a variable, transfer its ownership
		if ast.IsSym(value) {
			ctx.TransferOwnership(value.Str, target.Str)
		}
	}

	// If assigning to a field (target is a field access expression)
	// This would need more sophisticated analysis to detect
}

func (ctx *OwnershipContext) analyzeLambdaOwnership(args *ast.Value) {
	if ast.IsNil(args) {
		return
	}

	params := args.Car
	body := args.Cdr.Car

	ctx.EnterScope("lambda")

	// Parameters are borrowed
	for !ast.IsNil(params) && ast.IsCell(params) {
		param := params.Car
		if ast.IsSym(param) {
			ctx.DefineBorrowed(param.Str)
		}
		params = params.Cdr
	}

	// Analyze body
	ctx.AnalyzeOwnership(body)

	ctx.ExitScope()
}

func (ctx *OwnershipContext) analyzeConstructorOwnership(args *ast.Value) {
	// Constructor arguments: their ownership is transferred to the new object
	for !ast.IsNil(args) && ast.IsCell(args) {
		arg := args.Car
		ctx.AnalyzeOwnership(arg)

		// If arg is a variable, its ownership is transferred
		if ast.IsSym(arg) {
			// Mark as potentially transferred
			// Actual transfer happens when assigned to a field
		}

		args = args.Cdr
	}
}

// OwnershipClassString returns string representation of ownership class
func OwnershipClassString(c OwnershipClass) string {
	switch c {
	case OwnerLocal:
		return "local"
	case OwnerBorrowed:
		return "borrowed"
	case OwnerTransferred:
		return "transferred"
	case OwnerShared:
		return "shared"
	case OwnerWeak:
		return "weak"
	case OwnerConsumed:
		return "consumed"
	default:
		return "unknown"
	}
}
