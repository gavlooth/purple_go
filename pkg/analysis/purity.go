package analysis

import (
	"purple_go/pkg/ast"
)

// PurityLevel represents how pure an expression is
type PurityLevel int

const (
	PurityUnknown PurityLevel = iota
	PurityPure                // Definitely no side effects, no mutation
	PurityReadOnly            // May read mutable state but doesn't mutate
	PurityImpure              // May have side effects or mutate state
)

// PurityInfo holds purity analysis results for an expression
type PurityInfo struct {
	Level       PurityLevel
	MutatesVars []string // Variables this expression may mutate
	ReadsVars   []string // Variables this expression reads
}

// PurityAnalyzer performs purity analysis on expressions
type PurityAnalyzer struct {
	// Known pure primitives (no side effects, deterministic)
	PurePrimitives map[string]bool

	// Known read-only primitives (may read state but don't mutate)
	ReadOnlyPrimitives map[string]bool

	// Known impure primitives (I/O, mutation, etc.)
	ImpurePrimitives map[string]bool

	// Function purity cache
	FunctionPurity map[string]PurityLevel

	// Current analysis context
	currentScope *PurityScope
}

// PurityScope tracks purity within a scope
type PurityScope struct {
	Parent       *PurityScope
	LocalVars    map[string]bool      // Variables defined in this scope
	MutatedVars  map[string]bool      // Variables mutated in this scope
	VarPurity    map[string]PurityLevel // Known purity of variables
}

// NewPurityAnalyzer creates a new purity analyzer with default primitives
func NewPurityAnalyzer() *PurityAnalyzer {
	pa := &PurityAnalyzer{
		PurePrimitives:     make(map[string]bool),
		ReadOnlyPrimitives: make(map[string]bool),
		ImpurePrimitives:   make(map[string]bool),
		FunctionPurity:     make(map[string]PurityLevel),
	}

	// Pure primitives - no side effects, deterministic
	purePrims := []string{
		// Arithmetic
		"+", "-", "*", "/", "mod", "abs", "neg",
		// Comparison
		"<", ">", "<=", ">=", "=", "eq?", "equal?",
		// Boolean
		"not", "and", "or",
		// Type predicates
		"null?", "pair?", "int?", "float?", "char?", "symbol?",
		"list?", "number?", "boolean?", "procedure?",
		// List operations (non-mutating)
		"car", "cdr", "cons", "list", "length", "append",
		"reverse", "map", "filter", "fold", "foldr",
		"nth", "take", "drop", "zip",
		// Arithmetic conversions
		"int->float", "float->int", "char->int", "int->char",
		"floor", "ceil", "round", "truncate",
		// String/char operations
		"string-length", "string-ref", "string-append",
		"char-upcase", "char-downcase",
		// Control (pure variants)
		"if", "cond", "case", "let", "let*", "letrec",
		"lambda", "quote", "quasiquote",
		// Identity
		"identity", "const",
	}
	for _, p := range purePrims {
		pa.PurePrimitives[p] = true
	}

	// Read-only primitives - may read mutable state
	readOnlyPrims := []string{
		"box-get", "deref", "atom-deref",
		"vector-ref", "hash-ref",
	}
	for _, p := range readOnlyPrims {
		pa.ReadOnlyPrimitives[p] = true
	}

	// Impure primitives - side effects or mutation
	impurePrims := []string{
		// Mutation
		"set!", "box-set!", "set-car!", "set-cdr!",
		"vector-set!", "hash-set!",
		"atom-reset!", "atom-swap!", "atom-compare-and-set!",
		// I/O
		"display", "print", "newline", "read", "write",
		"read-char", "write-char", "read-line",
		"open-input-file", "open-output-file", "close-port",
		// Channels
		"chan-send!", "chan-recv!", "chan-close!",
		// Threading
		"thread", "thread-join", "go",
		// Other effects
		"error", "raise", "call/cc",
		"gensym", "random",
	}
	for _, p := range impurePrims {
		pa.ImpurePrimitives[p] = true
	}

	return pa
}

// Analyze returns the purity level of an expression
func (pa *PurityAnalyzer) Analyze(expr *ast.Value) PurityInfo {
	if pa.currentScope == nil {
		pa.currentScope = &PurityScope{
			LocalVars:   make(map[string]bool),
			MutatedVars: make(map[string]bool),
			VarPurity:   make(map[string]PurityLevel),
		}
	}
	return pa.analyzeExpr(expr)
}

// AnalyzeInScope analyzes with a fresh scope
func (pa *PurityAnalyzer) AnalyzeInScope(expr *ast.Value) PurityInfo {
	oldScope := pa.currentScope
	pa.currentScope = &PurityScope{
		Parent:      oldScope,
		LocalVars:   make(map[string]bool),
		MutatedVars: make(map[string]bool),
		VarPurity:   make(map[string]PurityLevel),
	}
	result := pa.analyzeExpr(expr)
	pa.currentScope = oldScope
	return result
}

func (pa *PurityAnalyzer) analyzeExpr(expr *ast.Value) PurityInfo {
	if expr == nil || ast.IsNil(expr) {
		return PurityInfo{Level: PurityPure}
	}

	switch expr.Tag {
	case ast.TInt, ast.TFloat, ast.TChar:
		return PurityInfo{Level: PurityPure}

	case ast.TSym:
		// Variable reference - pure read
		return PurityInfo{
			Level:     PurityPure,
			ReadsVars: []string{expr.Str},
		}

	case ast.TCell:
		return pa.analyzeApplication(expr)

	default:
		return PurityInfo{Level: PurityPure}
	}
}

func (pa *PurityAnalyzer) analyzeApplication(expr *ast.Value) PurityInfo {
	if !ast.IsCell(expr) {
		return PurityInfo{Level: PurityPure}
	}

	// Get the operator
	op := expr.Car
	args := expr.Cdr

	// Handle symbols (function calls, special forms)
	if ast.IsSym(op) {
		name := op.Str

		// Check for mutation operations
		if pa.ImpurePrimitives[name] {
			info := PurityInfo{Level: PurityImpure}
			// Track which variables are mutated
			if name == "set!" && ast.IsCell(args) && ast.IsSym(args.Car) {
				info.MutatesVars = append(info.MutatesVars, args.Car.Str)
			}
			return info
		}

		// Check for read-only operations
		if pa.ReadOnlyPrimitives[name] {
			return pa.analyzeArgs(args, PurityReadOnly)
		}

		// Check for pure primitives
		if pa.PurePrimitives[name] {
			return pa.analyzeSpecialForm(name, args)
		}

		// Unknown function - check cache or assume impure
		if cached, ok := pa.FunctionPurity[name]; ok {
			return pa.analyzeArgs(args, cached)
		}

		// Conservative: unknown functions are impure
		return pa.analyzeArgs(args, PurityImpure)
	}

	// Non-symbol operator - analyze it and args
	opInfo := pa.analyzeExpr(op)
	argsInfo := pa.analyzeArgs(args, PurityPure)
	return pa.combinePurity(opInfo, argsInfo)
}

func (pa *PurityAnalyzer) analyzeSpecialForm(name string, args *ast.Value) PurityInfo {
	switch name {
	case "if":
		// (if cond then else)
		return pa.analyzeAllArgs(args)

	case "let", "let*":
		// (let ((var val) ...) body)
		return pa.analyzeLetForm(args)

	case "lambda":
		// Lambdas are pure values (the body might not be, but creating one is)
		return PurityInfo{Level: PurityPure}

	case "quote", "quasiquote":
		return PurityInfo{Level: PurityPure}

	case "begin":
		// Sequence - purity is the worst of all expressions
		return pa.analyzeAllArgs(args)

	default:
		// Other pure primitives - analyze arguments
		return pa.analyzeAllArgs(args)
	}
}

func (pa *PurityAnalyzer) analyzeLetForm(args *ast.Value) PurityInfo {
	if !ast.IsCell(args) {
		return PurityInfo{Level: PurityPure}
	}

	bindings := args.Car
	body := args.Cdr

	// Create new scope for let
	oldScope := pa.currentScope
	pa.currentScope = &PurityScope{
		Parent:      oldScope,
		LocalVars:   make(map[string]bool),
		MutatedVars: make(map[string]bool),
		VarPurity:   make(map[string]PurityLevel),
	}

	combined := PurityInfo{Level: PurityPure}

	// Analyze bindings
	for b := bindings; ast.IsCell(b); b = b.Cdr {
		binding := b.Car
		if ast.IsCell(binding) && ast.IsSym(binding.Car) {
			varName := binding.Car.Str
			pa.currentScope.LocalVars[varName] = true

			// Analyze the value
			if ast.IsCell(binding.Cdr) {
				valInfo := pa.analyzeExpr(binding.Cdr.Car)
				pa.currentScope.VarPurity[varName] = valInfo.Level
				combined = pa.combinePurity(combined, valInfo)
			}
		}
	}

	// Analyze body
	bodyInfo := pa.analyzeAllArgs(body)
	combined = pa.combinePurity(combined, bodyInfo)

	pa.currentScope = oldScope
	return combined
}

func (pa *PurityAnalyzer) analyzeArgs(args *ast.Value, basePurity PurityLevel) PurityInfo {
	result := PurityInfo{Level: basePurity}
	for arg := args; ast.IsCell(arg); arg = arg.Cdr {
		argInfo := pa.analyzeExpr(arg.Car)
		result = pa.combinePurity(result, argInfo)
	}
	return result
}

func (pa *PurityAnalyzer) analyzeAllArgs(args *ast.Value) PurityInfo {
	result := PurityInfo{Level: PurityPure}
	for arg := args; ast.IsCell(arg); arg = arg.Cdr {
		argInfo := pa.analyzeExpr(arg.Car)
		result = pa.combinePurity(result, argInfo)
	}
	return result
}

func (pa *PurityAnalyzer) combinePurity(a, b PurityInfo) PurityInfo {
	result := PurityInfo{
		MutatesVars: append(a.MutatesVars, b.MutatesVars...),
		ReadsVars:   append(a.ReadsVars, b.ReadsVars...),
	}

	// Purity level is the "worst" (most impure) of the two
	if a.Level > b.Level {
		result.Level = a.Level
	} else {
		result.Level = b.Level
	}

	return result
}

// IsPure returns true if the expression is definitely pure
func (pa *PurityAnalyzer) IsPure(expr *ast.Value) bool {
	info := pa.Analyze(expr)
	return info.Level == PurityPure
}

// IsReadOnly returns true if the expression only reads (no mutation)
func (pa *PurityAnalyzer) IsReadOnly(expr *ast.Value) bool {
	info := pa.Analyze(expr)
	return info.Level <= PurityReadOnly
}

// RegisterPureFunction marks a function as known pure
func (pa *PurityAnalyzer) RegisterPureFunction(name string) {
	pa.FunctionPurity[name] = PurityPure
	pa.PurePrimitives[name] = true
}

// RegisterReadOnlyFunction marks a function as read-only
func (pa *PurityAnalyzer) RegisterReadOnlyFunction(name string) {
	pa.FunctionPurity[name] = PurityReadOnly
	pa.ReadOnlyPrimitives[name] = true
}

// GetMutatedVars returns variables mutated by an expression
func (pa *PurityAnalyzer) GetMutatedVars(expr *ast.Value) []string {
	info := pa.Analyze(expr)
	return info.MutatesVars
}

// PurityContext tracks purity during code generation
type PurityContext struct {
	Analyzer       *PurityAnalyzer
	PureDepth      int                    // Nesting depth of pure contexts
	ReadOnlyVars   map[string]bool        // Variables that are read-only in current context
	ExprPurity     map[*ast.Value]PurityInfo // Cached purity results
}

// NewPurityContext creates a new purity context for codegen
func NewPurityContext() *PurityContext {
	return &PurityContext{
		Analyzer:     NewPurityAnalyzer(),
		ReadOnlyVars: make(map[string]bool),
		ExprPurity:   make(map[*ast.Value]PurityInfo),
	}
}

// EnterPureContext marks entry into a pure context
func (ctx *PurityContext) EnterPureContext(readOnlyVars []string) {
	ctx.PureDepth++
	for _, v := range readOnlyVars {
		ctx.ReadOnlyVars[v] = true
	}
}

// ExitPureContext marks exit from a pure context
func (ctx *PurityContext) ExitPureContext(readOnlyVars []string) {
	ctx.PureDepth--
	if ctx.PureDepth == 0 {
		// Clear read-only vars when leaving outermost pure context
		for _, v := range readOnlyVars {
			delete(ctx.ReadOnlyVars, v)
		}
	}
}

// IsPureContext returns true if we're in a pure context
func (ctx *PurityContext) IsPureContext() bool {
	return ctx.PureDepth > 0
}

// IsVarReadOnly returns true if a variable is read-only in current context
func (ctx *PurityContext) IsVarReadOnly(varName string) bool {
	return ctx.ReadOnlyVars[varName]
}

// CanSkipSafetyChecks returns true if safety checks can be skipped for a variable
func (ctx *PurityContext) CanSkipSafetyChecks(varName string) bool {
	return ctx.IsPureContext() && ctx.IsVarReadOnly(varName)
}
