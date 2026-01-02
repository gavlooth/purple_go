package codegen

import (
	"fmt"
	"io"
	"strings"

	"purple_go/pkg/analysis"
	"purple_go/pkg/ast"
)

// CodeGenerator generates C99 code from AST
type CodeGenerator struct {
	w                io.Writer
	registry         *TypeRegistry
	escapeCtx        *analysis.AnalysisContext
	shapeCtx         *analysis.ShapeContext
	rcOptCtx         *analysis.RCOptContext         // RC optimization context
	summaryCtx       *analysis.SummaryAnalyzer      // Interprocedural analysis
	concurrencyCtx   *analysis.ConcurrencyAnalyzer  // Concurrency ownership
	reuseCtx         *analysis.ReuseAnalyzer        // Perceus reuse analysis
	livenessCtx      *analysis.LivenessContext      // Liveness analysis for early frees
	ownershipCtx     *analysis.OwnershipContext     // Ownership-driven memory management
	regionCtx        *analysis.RegionAnalyzer       // Region-based memory management
	purityCtx        *analysis.PurityContext        // Purity analysis for zero-cost access (Vale-style)
	arenaGen         *ArenaCodeGenerator
	stats            *OptimizationStats             // Optimization statistics
	tempCounter      int
	indentLevel      int
	useArenaFallback bool
	enableRCOpt      bool   // Enable RC optimization (Lobster-style)
	enableRegions    bool   // Enable region-based allocation
	enableBorrowRef     bool   // Enable GenRef for borrowed references (O(1) use-after-free detection)
	enablePurity     bool   // Enable purity analysis for zero-cost access in pure contexts
	enableTethering  bool   // Enable scope tethering for repeated accesses
	borrowRefBindings   map[string]bool // Variables that use GenRef instead of RC
	tetheredVars     map[string]bool // Variables that are tethered in current scope
}

// NewCodeGenerator creates a new code generator
func NewCodeGenerator(w io.Writer) *CodeGenerator {
	registry := NewTypeRegistry()
	registry.InitDefaultTypes()
	return &CodeGenerator{
		w:                w,
		registry:         registry,
		escapeCtx:        analysis.NewAnalysisContext(),
		shapeCtx:         analysis.NewShapeContext(),
		rcOptCtx:         analysis.NewRCOptContext(),
		summaryCtx:       analysis.NewSummaryAnalyzer(),
		concurrencyCtx:   analysis.NewConcurrencyAnalyzer(),
		reuseCtx:         analysis.NewReuseAnalyzer(),
		livenessCtx:      analysis.NewLivenessContext(),
		ownershipCtx:     analysis.NewOwnershipContext(registry),
		regionCtx:        analysis.NewRegionAnalyzer(),
		purityCtx:        analysis.NewPurityContext(),
		arenaGen:         NewArenaCodeGenerator(),
		stats:            NewOptimizationStats(),
		useArenaFallback: false, // Arena is opt-in; default to non-arena strategies
		enableRCOpt:      true,  // Enable Lobster-style RC optimization
		enableRegions:    false, // Region allocation is opt-in
		enableBorrowRef:     true,  // Enable GenRef for borrowed references
		enablePurity:     true,  // Enable purity analysis for zero-cost access
		enableTethering:  true,  // Enable scope tethering for repeated accesses
		borrowRefBindings:   make(map[string]bool),
		tetheredVars:     make(map[string]bool),
	}
}

// NewCodeGeneratorWithGlobalRegistry creates a code generator using the global type registry
// This allows access to user-defined types from deftype declarations
func NewCodeGeneratorWithGlobalRegistry(w io.Writer) *CodeGenerator {
	registry := GlobalRegistry()
	return &CodeGenerator{
		w:                w,
		registry:         registry,
		escapeCtx:        analysis.NewAnalysisContext(),
		shapeCtx:         analysis.NewShapeContext(),
		rcOptCtx:         analysis.NewRCOptContext(),
		summaryCtx:       analysis.NewSummaryAnalyzer(),
		concurrencyCtx:   analysis.NewConcurrencyAnalyzer(),
		reuseCtx:         analysis.NewReuseAnalyzer(),
		livenessCtx:      analysis.NewLivenessContext(),
		ownershipCtx:     analysis.NewOwnershipContext(registry),
		regionCtx:        analysis.NewRegionAnalyzer(),
		purityCtx:        analysis.NewPurityContext(),
		arenaGen:         NewArenaCodeGenerator(),
		stats:            NewOptimizationStats(),
		useArenaFallback: false,
		enableRCOpt:      true,
		enableRegions:    false,
		enableBorrowRef:     true,
		enablePurity:     true,
		enableTethering:  true,
		borrowRefBindings:   make(map[string]bool),
		tetheredVars:     make(map[string]bool),
	}
}

// GetCycleStatusForType returns the cycle status for a user-defined type
// Uses TypeRegistry's analysis for cycle detection with weak edge breaking
func (g *CodeGenerator) GetCycleStatusForType(typeName string) CycleStatus {
	if g.registry == nil {
		return CycleStatusNone
	}
	return g.registry.GetCycleStatus(typeName)
}

// ShouldUseArenaForType returns true if arena allocation should be used for a type
// based on its cycle status (unbroken cycles require arena or SCC)
func (g *CodeGenerator) ShouldUseArenaForType(typeName string) bool {
	status := g.GetCycleStatusForType(typeName)
	return status == CycleStatusUnbroken
}

// SetRCOptimization enables or disables RC optimization
func (g *CodeGenerator) SetRCOptimization(enabled bool) {
	g.enableRCOpt = enabled
}

// SetArenaFallback enables or disables arena fallback for cyclic shapes
func (g *CodeGenerator) SetArenaFallback(enabled bool) {
	g.useArenaFallback = enabled
}

// SetBorrowRefEnabled enables or disables GenRef for borrowed references
func (g *CodeGenerator) SetBorrowRefEnabled(enabled bool) {
	g.enableBorrowRef = enabled
}

// MarkAsBorrowRef marks a variable as using GenRef instead of RC
func (g *CodeGenerator) MarkAsBorrowRef(varName string) {
	if g.borrowRefBindings == nil {
		g.borrowRefBindings = make(map[string]bool)
	}
	g.borrowRefBindings[varName] = true
}

// IsBorrowRef returns true if the variable uses GenRef
func (g *CodeGenerator) IsBorrowRef(varName string) bool {
	if g.borrowRefBindings == nil {
		return false
	}
	return g.borrowRefBindings[varName]
}

// BorrowRefVarName returns the GenRef variable name for a borrowed variable
func (g *CodeGenerator) BorrowRefVarName(varName string) string {
	return fmt.Sprintf("_ref_%s", varName)
}

// SetPurityEnabled enables or disables purity analysis
func (g *CodeGenerator) SetPurityEnabled(enabled bool) {
	g.enablePurity = enabled
}

// SetTetheringEnabled enables or disables scope tethering
func (g *CodeGenerator) SetTetheringEnabled(enabled bool) {
	g.enableTethering = enabled
}

// IsPureContext returns true if we're currently in a pure context
func (g *CodeGenerator) IsPureContext() bool {
	if !g.enablePurity || g.purityCtx == nil {
		return false
	}
	return g.purityCtx.IsPureContext()
}

// CanSkipSafetyChecks returns true if safety checks can be skipped for a variable
// This is the case in pure contexts where the variable is read-only
func (g *CodeGenerator) CanSkipSafetyChecks(varName string) bool {
	if !g.enablePurity || g.purityCtx == nil {
		return false
	}
	return g.purityCtx.CanSkipSafetyChecks(varName)
}

// IsTethered returns true if the variable is currently tethered
func (g *CodeGenerator) IsTethered(varName string) bool {
	if !g.enableTethering || g.tetheredVars == nil {
		return false
	}
	return g.tetheredVars[varName]
}

// TetherVar marks a variable as tethered in the current scope
func (g *CodeGenerator) TetherVar(varName string) {
	if g.tetheredVars == nil {
		g.tetheredVars = make(map[string]bool)
	}
	g.tetheredVars[varName] = true
}

// UntetherVar removes the tethered status of a variable
func (g *CodeGenerator) UntetherVar(varName string) {
	if g.tetheredVars != nil {
		delete(g.tetheredVars, varName)
	}
}

// ShouldUseTethering returns true if tethering should be used for a borrowed reference
// Tethering is beneficial when a reference is accessed multiple times in a scope
func (g *CodeGenerator) ShouldUseTethering(varName string, accessCount int) bool {
	if !g.enableTethering {
		return false
	}
	// Tethering is beneficial for multiple accesses (amortizes tether/untether cost)
	return accessCount > 1
}

// GenerateTetheredAccess generates code for accessing a tethered variable
// If the variable is tethered, generation checks are skipped
func (g *CodeGenerator) GenerateTetheredAccess(varName string) string {
	if g.IsTethered(varName) {
		// Fast path: variable is tethered, skip generation check
		return varName
	}
	if g.CanSkipSafetyChecks(varName) {
		// Pure context: zero-cost access
		return varName
	}
	// Normal access with safety checks
	return varName
}

// GetStats returns the optimization statistics
func (g *CodeGenerator) GetStats() *OptimizationStats {
	return g.stats
}

// GetStatsSummary returns a one-line summary of optimization statistics
func (g *CodeGenerator) GetStatsSummary() string {
	if g.stats == nil {
		return "No statistics available"
	}
	return g.stats.Summary()
}

// GetStatsReport returns a full statistics report
func (g *CodeGenerator) GetStatsReport() string {
	if g.stats == nil {
		return "No statistics available"
	}
	return g.stats.String()
}

// AnalyzeFunction registers a function's summary for interprocedural analysis
func (g *CodeGenerator) AnalyzeFunction(name string, params *ast.Value, body *ast.Value) *analysis.FunctionSummary {
	if g.summaryCtx == nil {
		return nil
	}
	return g.summaryCtx.AnalyzeFunction(name, params, body)
}

// GetParamOwnership returns the ownership class for a function parameter at a call site
func (g *CodeGenerator) GetParamOwnership(funcName string, paramIdx int) analysis.OwnershipClass {
	if g.summaryCtx == nil || g.summaryCtx.Registry == nil {
		return analysis.OwnerBorrowed
	}
	return g.summaryCtx.Registry.GetParamOwnership(funcName, paramIdx)
}

// GetReturnOwnership returns the ownership class for a function's return value
func (g *CodeGenerator) GetReturnOwnership(funcName string) analysis.OwnershipClass {
	if g.summaryCtx == nil || g.summaryCtx.Registry == nil {
		return analysis.OwnerFresh
	}
	return g.summaryCtx.Registry.GetReturnOwnership(funcName)
}

// AnalyzeConcurrency performs concurrency analysis on an expression
func (g *CodeGenerator) AnalyzeConcurrency(expr *ast.Value) {
	if g.concurrencyCtx != nil {
		g.concurrencyCtx.Analyze(expr)
	}
}

// NeedsAtomicRC returns true if a variable needs atomic reference counting
func (g *CodeGenerator) NeedsAtomicRC(varName string) bool {
	if g.concurrencyCtx == nil {
		return false
	}
	return g.concurrencyCtx.Ctx.NeedsAtomicRC(varName)
}

// IsTransferred returns true if a variable's ownership has been transferred (e.g., via chan-send!)
func (g *CodeGenerator) IsTransferred(varName string) bool {
	if g.concurrencyCtx == nil {
		return false
	}
	return g.concurrencyCtx.Ctx.GetLocality(varName) == analysis.LocalityTransferred
}

// AnalyzeReuse performs reuse analysis on an expression
func (g *CodeGenerator) AnalyzeReuse(expr *ast.Value) {
	if g.reuseCtx != nil {
		g.reuseCtx.Analyze(expr)
	}
}

// TryReuse attempts to find a reuse candidate for an allocation
func (g *CodeGenerator) TryReuse(allocVar, allocType string, line int) *analysis.ReuseCandidate {
	if g.reuseCtx == nil {
		return nil
	}
	return g.reuseCtx.Ctx.TryReuse(allocVar, allocType, line)
}

// GetReuseFor returns the variable that can be reused for an allocation, if any
func (g *CodeGenerator) GetReuseFor(allocVar string) (string, bool) {
	if g.reuseCtx == nil {
		return "", false
	}
	return g.reuseCtx.Ctx.GetReuse(allocVar)
}

// AddPendingFree marks a variable as pending for free (available for reuse)
func (g *CodeGenerator) AddPendingFree(name, typeName string) {
	if g.reuseCtx != nil {
		g.reuseCtx.Ctx.AddPendingFree(name, typeName)
	}
}

// GenerateRCOperation generates the appropriate reference count operation
// Uses atomic operations for shared variables, regular operations otherwise
// Checks RC optimization context to elide unnecessary operations
func (g *CodeGenerator) GenerateRCOperation(varName string, op string) string {
	// Check if we can elide this operation entirely (Lobster-style optimization)
	if g.enableRCOpt && g.rcOptCtx != nil {
		switch op {
		case "inc":
			if !g.rcOptCtx.ShouldEmitIncRef(varName) {
				return fmt.Sprintf("/* inc_ref(%s) elided - %s */", varName,
					g.rcOptCtx.GetElisionReason(varName, "inc"))
			}
		case "dec":
			if !g.rcOptCtx.ShouldEmitDecRef(varName) {
				return fmt.Sprintf("/* dec_ref(%s) elided - %s */", varName,
					g.rcOptCtx.GetElisionReason(varName, "dec"))
			}
			// Check if we can use free_unique instead of dec_ref
			opt := g.rcOptCtx.GetOptimizedDecRef(varName)
			if opt == analysis.RCOptDirectFree {
				return fmt.Sprintf("free_unique(%s)", varName)
			}
		}
	}

	if g.NeedsAtomicRC(varName) {
		switch op {
		case "inc":
			return fmt.Sprintf("atomic_inc_ref(%s)", varName)
		case "dec":
			return fmt.Sprintf("atomic_dec_ref(%s)", varName)
		}
	}
	switch op {
	case "inc":
		return fmt.Sprintf("inc_ref(%s)", varName)
	case "dec":
		return fmt.Sprintf("dec_ref(%s)", varName)
	}
	return ""
}

// GenerateAllocation generates an allocation, potentially reusing freed memory
func (g *CodeGenerator) GenerateAllocation(varName, allocType string, allocExpr string) string {
	if freeVar, ok := g.GetReuseFor(varName); ok {
		// Reuse available
		return fmt.Sprintf("reuse_as_%s(%s, %s)", allocType, freeVar, allocExpr)
	}
	return allocExpr
}

func (g *CodeGenerator) inferType(val *ast.Value) string {
	if val == nil || ast.IsNil(val) {
		return "Obj"
	}
	switch val.Tag {
	case ast.TInt:
		return "int"
	case ast.TFloat:
		return "float"
	case ast.TChar:
		return "char"
	case ast.TCode:
		return "Obj"
	case ast.TCell:
		if ast.IsSym(val.Car) {
			switch val.Car.Str {
			case "cons", "list":
				return "pair"
			case "box":
				return "box"
			case "lambda":
				return "closure"
			}
			if len(val.Car.Str) > 3 && val.Car.Str[:3] == "mk-" {
				return "Obj"
			}
		}
	}
	return "Obj"
}

func (g *CodeGenerator) generateReuseExpr(candidate *analysis.ReuseCandidate, initExpr *ast.Value, fallback string) (string, bool) {
	switch candidate.AllocType {
	case "int":
		if ast.IsInt(initExpr) {
			return fmt.Sprintf("reuse_as_int(%s, %d)", candidate.FreeVar, initExpr.Int), true
		}
	case "pair":
		if ast.IsCell(initExpr) && ast.IsSym(initExpr.Car) && initExpr.Car.Str == "cons" {
			aExpr := g.ValueToCExpr(initExpr.Cdr.Car)
			bExpr := g.ValueToCExpr(initExpr.Cdr.Cdr.Car)
			return fmt.Sprintf("reuse_as_pair(%s, %s, %s)", candidate.FreeVar, aExpr, bExpr), true
		}
	case "box":
		if ast.IsCell(initExpr) && ast.IsSym(initExpr.Car) && initExpr.Car.Str == "box" {
			valExpr := g.ValueToCExpr(initExpr.Cdr.Car)
			return fmt.Sprintf("reuse_as_box(%s, %s)", candidate.FreeVar, valExpr), true
		}
	}
	return fallback, false
}

func (g *CodeGenerator) emit(format string, args ...interface{}) {
	fmt.Fprintf(g.w, format, args...)
}

func (g *CodeGenerator) indent() string {
	return strings.Repeat("    ", g.indentLevel)
}

func (g *CodeGenerator) newTemp() string {
	g.tempCounter++
	return fmt.Sprintf("_t%d", g.tempCounter)
}

// ValueToCExpr converts a Value to a C expression string
func (g *CodeGenerator) ValueToCExpr(v *ast.Value) string {
	if v == nil || ast.IsNil(v) {
		return "NULL"
	}
	switch v.Tag {
	case ast.TCode:
		return v.Str
	case ast.TInt:
		return fmt.Sprintf("mk_int_unboxed(%d)", v.Int)
	case ast.TFloat:
		return fmt.Sprintf("mk_float(%g)", v.Float)
	case ast.TChar:
		return fmt.Sprintf("mk_char_unboxed(%d)", v.Int)
	case ast.TCell:
		carExpr := g.ValueToCExpr(v.Car)
		cdrExpr := g.ValueToCExpr(v.Cdr)
		return fmt.Sprintf("mk_pair(%s, %s)", carExpr, cdrExpr)
	default:
		return "NULL"
	}
}

// ValueToCExprStack converts a Value to a C expression, using stack allocation
// for non-escaping primitives (int, float, char)
func (g *CodeGenerator) ValueToCExprStack(v *ast.Value) string {
	if v == nil || ast.IsNil(v) {
		return "NULL"
	}
	switch v.Tag {
	case ast.TCode:
		return v.Str
	case ast.TInt:
		return fmt.Sprintf("mk_int_unboxed(%d)", v.Int)  // Unboxed is better than stack
	case ast.TFloat:
		return fmt.Sprintf("mk_float_stack(%g)", v.Float)
	case ast.TChar:
		return fmt.Sprintf("mk_char_unboxed(%d)", v.Int)  // Unboxed is better than stack
	case ast.TCell:
		// Pairs cannot use stack allocation (child references need heap)
		carExpr := g.ValueToCExpr(v.Car)
		cdrExpr := g.ValueToCExpr(v.Cdr)
		return fmt.Sprintf("mk_pair(%s, %s)", carExpr, cdrExpr)
	default:
		return "NULL"
	}
}

// isPrimitiveValue returns true if the value is a primitive (int, float, char)
// that can potentially use stack allocation
func isPrimitiveValue(v *ast.Value) bool {
	if v == nil {
		return false
	}
	return v.Tag == ast.TInt || v.Tag == ast.TFloat || v.Tag == ast.TChar
}

// LiftValue converts a Value to a code Value
func (g *CodeGenerator) LiftValue(v *ast.Value) *ast.Value {
	if v == nil || ast.IsNil(v) {
		return ast.NewCode("NULL")
	}
	switch v.Tag {
	case ast.TCode:
		return v
	case ast.TInt:
		return ast.NewCode(fmt.Sprintf("mk_int_unboxed(%d)", v.Int))
	case ast.TFloat:
		return ast.NewCode(fmt.Sprintf("mk_float(%g)", v.Float))
	case ast.TChar:
		return ast.NewCode(fmt.Sprintf("mk_char_unboxed(%d)", v.Int))
	case ast.TSym:
		return ast.NewCode(fmt.Sprintf("mk_sym(\"%s\")", v.Str))
	case ast.TCell:
		carCode := g.LiftValue(v.Car)
		cdrCode := g.LiftValue(v.Cdr)
		return ast.NewCode(fmt.Sprintf("mk_pair(%s, %s)", carCode.Str, cdrCode.Str))
	default:
		return ast.NewCode("NULL")
	}
}

// EmitCCall generates a C function call
func (g *CodeGenerator) EmitCCall(fn string, a, b *ast.Value) *ast.Value {
	aStr := g.ValueToCExpr(a)
	bStr := g.ValueToCExpr(b)
	return ast.NewCode(fmt.Sprintf("%s(%s, %s)", fn, aStr, bStr))
}

// GenerateCall generates a function call with ownership-aware RC.
func (g *CodeGenerator) GenerateCall(fn string, args []*ast.Value) string {
	var sb strings.Builder

	var summary *analysis.FunctionSummary
	if g.summaryCtx != nil && g.summaryCtx.Registry != nil {
		summary = g.summaryCtx.Registry.Lookup(fn)
	}

	argExprs := make([]string, len(args))
	for i, arg := range args {
		argExprs[i] = g.ValueToCExpr(arg)
	}

	for i, arg := range args {
		if !ast.IsSym(arg) {
			continue
		}
		varName := arg.Str

		ownership := analysis.OwnerBorrowed
		if summary != nil && i < len(summary.Params) {
			ownership = summary.Params[i].Ownership
		}

		switch ownership {
		case analysis.OwnerConsumed:
			// Ownership transfers to callee - no inc_ref needed
			// Caller should NOT dec_ref after call (callee will free)
			if g.concurrencyCtx != nil {
				g.concurrencyCtx.Ctx.MarkTransferred(varName)
			}
			if g.rcOptCtx != nil {
				g.rcOptCtx.MarkConsumed(varName)
			}
			// Lobster-style: mark as consumed in ownership context
			if g.ownershipCtx != nil {
				g.ownershipCtx.ConsumeOwnership(varName, fn)
			}
			if g.stats != nil {
				g.stats.RCIncElided++ // Skipped inc_ref
			}
			sb.WriteString(fmt.Sprintf("/* %s consumed by %s */\n", varName, fn))
		case analysis.OwnerBorrowed:
			// Borrowed = callee just reads, caller keeps ownership
			// NO inc_ref needed! Caller will dec_ref after call returns
			if g.stats != nil {
				g.stats.RCIncElided++ // Skipped inc_ref (borrowed)
			}
			sb.WriteString(fmt.Sprintf("/* %s borrowed by %s */\n", varName, fn))
		case analysis.OwnerShared:
			// Shared = both caller and callee keep a reference
			// NEED inc_ref so callee can keep it after call
			if g.rcOptCtx == nil || !g.rcOptCtx.IsUnique(varName) {
				rcOp := g.GenerateRCOperation(varName, "inc")
				sb.WriteString(rcOp + ";\n")
			} else if g.stats != nil {
				g.stats.RCIncElided++ // Unique, no inc needed
			}
		}
	}

	sb.WriteString(fmt.Sprintf("%s(%s)", fn, strings.Join(argExprs, ", ")))
	return sb.String()
}

// GenerateLet generates code for a let expression with ASAP memory management
func (g *CodeGenerator) GenerateLet(bindings []struct {
	sym *ast.Value
	val *ast.Value
}, body *ast.Value) string {
	// Run liveness analysis on body to find last use of each variable
	varNames := make([]string, len(bindings))
	for i, bi := range bindings {
		varNames[i] = bi.sym.Str
	}
	freePlacements := analysis.ComputeFreePlacements(body, varNames)

	// Region tracking: Enter a new region for this let scope
	var region *analysis.Region
	if g.regionCtx != nil {
		region = g.regionCtx.EnterScope("let")
		for _, bi := range bindings {
			g.regionCtx.AllocateIn(bi.sym.Str)
		}
	}

	// Analyze the expression for escape and shape
	needsArena := false
	for _, bi := range bindings {
		g.escapeCtx.AddVar(bi.sym.Str)
		g.shapeCtx.AnalyzeShapes(bi.val)
		g.shapeCtx.AddShape(bi.sym.Str, g.shapeCtx.ResultShape)

		// RC Optimization: Track aliases and uniqueness
		if g.enableRCOpt {
			if ast.IsSym(bi.val) {
				if g.rcOptCtx.IsUnique(bi.val.Str) {
					g.rcOptCtx.TransferUniqueness(bi.val.Str, bi.sym.Str)
				} else {
					// Value is a variable reference - creates an alias
					g.rcOptCtx.DefineAlias(bi.sym.Str, bi.val.Str)
				}
			} else if g.rcOptCtx.IsFreshAllocation(bi.val) {
				// Fresh allocation - starts as unique
				g.rcOptCtx.DefineVar(bi.sym.Str)
			} else {
				g.rcOptCtx.DefineVarNonUnique(bi.sym.Str)
			}
		}

		// Ownership analysis: Track ownership for memory management
		if g.ownershipCtx != nil {
			if g.rcOptCtx != nil && g.rcOptCtx.IsFreshAllocation(bi.val) {
				// Fresh allocation - locally owned
				g.ownershipCtx.DefineOwned(bi.sym.Str)
			} else if ast.IsSym(bi.val) {
				// Variable reference - mark as borrowed unless transferred
				g.ownershipCtx.DefineBorrowed(bi.sym.Str)
				// GenRef: Track borrowed references for O(1) use-after-free detection
				if g.enableBorrowRef {
					g.MarkAsBorrowRef(bi.sym.Str)
				}
			} else {
				// Other expressions - conservatively mark as owned
				g.ownershipCtx.DefineOwned(bi.sym.Str)
			}
		}

		// Check if arena fallback is needed
		if g.useArenaFallback {
			shape := g.shapeCtx.ResultShape
			if shape == analysis.ShapeCyclic || shape == analysis.ShapeUnknown {
				needsArena = true
			}
		}
	}

	// Purity Analysis: Check if body is pure for zero-cost access (Vale-style)
	var pureVars []string
	if g.enablePurity && g.purityCtx != nil {
		bodyPurity := g.purityCtx.Analyzer.Analyze(body)
		if bodyPurity.Level == analysis.PurityPure {
			// Body is pure - all bound variables are read-only
			for _, bi := range bindings {
				pureVars = append(pureVars, bi.sym.Str)
			}
			g.purityCtx.EnterPureContext(pureVars)
			if g.stats != nil {
				g.stats.PurityPureExprs++
				g.stats.PurityReadOnlyVars += len(pureVars)
			}
		}
	}

	// Scope Tethering: For borrowed references accessed multiple times
	var tetheredInScope []string
	if g.enableTethering {
		// Count accesses for each variable in body
		accessCounts := countVariableAccesses(body)
		for _, bi := range bindings {
			varName := bi.sym.Str
			// Tether borrowed references that are accessed > 1 time
			if g.IsBorrowRef(varName) && accessCounts[varName] > 1 {
				tetheredInScope = append(tetheredInScope, varName)
				g.TetherVar(varName)
				if g.stats != nil {
					g.stats.TetheredVars++
				}
			}
		}
	}

	// Convert bindings for arena generator
	arenaBindings := make([]struct {
		sym        *ast.Value
		val        string
		isStack    bool // Track if stack-allocated (no free needed)
		isBorrowRef   bool // Track if using GenRef for borrowed reference
		isTethered bool // Track if tethered for repeated access
	}, len(bindings))

	for i, bi := range bindings {
		valStr := ""
		useStack := false
		useGenRef := g.IsBorrowRef(bi.sym.Str)
		useTethered := g.IsTethered(bi.sym.Str)

		// Check if this binding can use stack allocation
		usage := g.escapeCtx.FindVar(bi.sym.Str)
		canStackAlloc := false
		if usage != nil && usage.Escape == analysis.EscapeNone && !usage.CapturedByLambda {
			canStackAlloc = true
		}

		if ast.IsCode(bi.val) {
			valStr = bi.val.Str
		} else if canStackAlloc && isPrimitiveValue(bi.val) {
			// Use stack allocation for non-escaping primitives
			valStr = g.ValueToCExprStack(bi.val)
			useStack = true
		} else {
			valStr = g.ValueToCExpr(bi.val)
		}
		arenaBindings[i].sym = bi.sym
		arenaBindings[i].val = valStr
		arenaBindings[i].isStack = useStack
		arenaBindings[i].isBorrowRef = useGenRef
		arenaBindings[i].isTethered = useTethered
	}

	bodyStr := ""
	if ast.IsCode(body) {
		bodyStr = body.Str
	} else {
		bodyStr = g.ValueToCExpr(body)
	}

	// Use arena generator if needed
	if needsArena {
		// Convert to format expected by arena generator
		arenaInput := make([]struct {
			sym *ast.Value
			val string
		}, len(arenaBindings))
		for i, bi := range arenaBindings {
			arenaInput[i].sym = bi.sym
			arenaInput[i].val = bi.val
		}
		return g.arenaGen.GenerateArenaLet(arenaInput, bodyStr, true)
	}

	typeNames := make([]string, len(bindings))
	for i, bi := range bindings {
		typeNames[i] = g.inferType(bi.val)
		if g.reuseCtx != nil {
			g.reuseCtx.Ctx.RegisterAllocation(bi.sym.Str, typeNames[i], 0)
		}
	}

	// Standard ASAP code generation
	var sb strings.Builder
	sb.WriteString("({\n")

	// Generate declarations
	for i, bi := range arenaBindings {
		varName := bi.sym.Str
		valStr := bi.val
		typeName := typeNames[i]

		// GenRef: For borrowed references, create a GenRef for O(1) use-after-free detection
		if bi.isBorrowRef {
			refVarName := g.BorrowRefVarName(varName)
			sb.WriteString(fmt.Sprintf("    /* BorrowRef: IPGE-validated borrowed reference */\n"))
			sb.WriteString(fmt.Sprintf("    BorrowRef* %s = borrow_create(%s, \"borrow:%s\");\n", refVarName, valStr, varName))
			sb.WriteString(fmt.Sprintf("    Obj* %s = borrow_get(%s); /* validated access */\n", varName, refVarName))
			// Scope Tethering: If this var is accessed multiple times, tether it
			if bi.isTethered {
				sb.WriteString(fmt.Sprintf("    tether_obj(%s); /* Vale-style: skip gen checks while tethered */\n", varName))
				if g.stats != nil {
					g.stats.TetheredVars++
				}
			}
			if g.stats != nil {
				g.stats.BorrowRefCreated++
			}
			continue
		}

		// Purity optimization: In pure context, skip safety setup for read-only vars
		// Note: This only applies to borrowed references (variable references), not fresh allocations
		// Fresh allocations should still go through normal path for reuse optimization
		if g.IsPureContext() && g.CanSkipSafetyChecks(varName) && ast.IsSym(bindings[i].val) {
			sb.WriteString(fmt.Sprintf("    /* Pure context: zero-cost access for %s */\n", varName))
			sb.WriteString(fmt.Sprintf("    Obj* %s = %s; /* no safety checks needed */\n", varName, valStr))
			if g.stats != nil {
				g.stats.PurityChecksSkipped++
			}
			continue
		}

		// Track stack vs heap allocation
		if bi.isStack {
			if g.stats != nil {
				g.stats.StackAllocations++
			}
		} else if isPrimitiveValue(bindings[i].val) {
			if g.stats != nil {
				g.stats.HeapAllocations++
			}
		}

		if g.reuseCtx != nil {
			if candidate := g.reuseCtx.Ctx.FindBestReuse(typeName, 0); candidate != nil {
				reuseExpr, ok := g.generateReuseExpr(candidate, bindings[i].val, valStr)
				if ok {
					candidate.AllocVar = varName
					g.reuseCtx.Ctx.Reuses[varName] = candidate.FreeVar
					g.reuseCtx.Ctx.ConsumePendingFree(candidate.FreeVar)
					sb.WriteString(fmt.Sprintf("    /* Reuse %s for %s */\n", candidate.FreeVar, varName))
					sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", varName, reuseExpr))
					if g.stats != nil {
						g.stats.MemoryReused++
					}
					continue
				}
			}
		}

		sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", varName, valStr))
	}

	sb.WriteString(fmt.Sprintf("    Obj* _res = %s;\n", bodyStr))

	// Generate frees based on analysis
	for i := len(bindings) - 1; i >= 0; i-- {
		bi := bindings[i]
		varName := bi.sym.Str

		// GenRef: Release the GenRef for borrowed references
		if arenaBindings[i].isBorrowRef {
			// Untether before releasing if tethered
			if arenaBindings[i].isTethered {
				sb.WriteString(fmt.Sprintf("    untether_obj(%s); /* Vale-style: scope tether released */\n", varName))
			}
			refVarName := g.BorrowRefVarName(varName)
			sb.WriteString(fmt.Sprintf("    borrow_release(%s); /* release borrowed ref */\n", refVarName))
			if g.stats != nil {
				g.stats.BorrowRefReleased++
			}
			continue
		}

		// Stack-allocated values don't need freeing
		if arenaBindings[i].isStack {
			sb.WriteString(fmt.Sprintf("    /* %s stack-allocated - no free */\n", varName))
			continue
		}

		// Check liveness: if lastUse == -1, variable was never used
		lastUse := freePlacements[varName]
		if lastUse == -1 {
			// Variable defined but never used - could be freed earlier but just note it
			sb.WriteString(fmt.Sprintf("    /* %s never used - could free immediately after def */\n", varName))
			if g.stats != nil {
				g.stats.LivenessUnused++
			}
		}

		usage := g.escapeCtx.FindVar(varName)
		shapeInfo := g.shapeCtx.FindShape(varName)

		isCaptured := usage != nil && usage.CapturedByLambda
		escapeClass := analysis.EscapeNone
		if usage != nil {
			escapeClass = usage.Escape
		}
		shape := analysis.ShapeUnknown
		if shapeInfo != nil {
			shape = shapeInfo.Shape
		}

		// RC Optimization: Get optimized free function
		var freeFn string
		var rcOptComment string

		if isCaptured {
			sb.WriteString(fmt.Sprintf("    /* %s captured by closure - no free */\n", varName))
			continue
		}
		if escapeClass == analysis.EscapeGlobal {
			sb.WriteString(fmt.Sprintf("    /* %s escapes to return - no free */\n", varName))
			if g.stats != nil {
				g.stats.EscapeGlobal++
			}
			continue
		}

		// Check ownership for free decision
		if g.ownershipCtx != nil {
			// Lobster-style: check if consumed first
			if g.ownershipCtx.IsConsumed(varName) {
				consumer := g.ownershipCtx.GetConsumer(varName)
				sb.WriteString(fmt.Sprintf("    /* %s consumed by %s - no free */\n", varName, consumer))
				if g.stats != nil {
					g.stats.RCDecElided++ // Skipped dec_ref (consumed)
				}
				continue
			}

			ownerInfo := g.ownershipCtx.GetOwnership(varName)
			if ownerInfo != nil {
				switch ownerInfo.Class {
				case analysis.OwnerBorrowed:
					sb.WriteString(fmt.Sprintf("    /* %s borrowed - no free */\n", varName))
					if g.stats != nil {
						g.stats.OwnerBorrowedSkips++
					}
					continue
				case analysis.OwnerTransferred:
					sb.WriteString(fmt.Sprintf("    /* %s transferred to %s - no free */\n", varName, ownerInfo.TransferredTo))
					if g.stats != nil {
						g.stats.OwnerTransferredSkips++
					}
					continue
				case analysis.OwnerWeak:
					sb.WriteString(fmt.Sprintf("    /* %s weak reference - no free */\n", varName))
					if g.stats != nil {
						g.stats.OwnerWeakSkips++
					}
					continue
				case analysis.OwnerConsumed:
					// Already handled by IsConsumed above
					continue
				case analysis.OwnerLocal:
					if g.stats != nil {
						g.stats.OwnerLocalFrees++
					}
				}
			}
		}

		if g.enableRCOpt {
			if g.IsTransferred(varName) {
				if g.rcOptCtx != nil {
					g.rcOptCtx.RecordTransferSkip()
				}
				sb.WriteString(fmt.Sprintf("    /* %s transferred, no RC */\n", varName))
				if g.stats != nil {
					g.stats.OwnerTransferredSkips++
				}
				continue
			}

			freeFn = g.rcOptCtx.GetFreeFunction(varName, shape)
			if freeFn == "" {
				// RC optimization eliminated this free
				rcOptComment = fmt.Sprintf("    /* %s: RC elided (borrowed/alias) */\n", varName)
				if g.stats != nil {
					g.stats.RCDecElided++
				}
			} else if freeFn == "free_unique" {
				rcOptComment = fmt.Sprintf("    %s(%s); /* RC opt: proven unique, lastUse=%d */\n", freeFn, varName, lastUse)
				if g.stats != nil {
					g.stats.RCUniqueDirectFree++
				}
			} else {
				rcOptComment = fmt.Sprintf("    %s(%s); /* ASAP Clean (shape: %s, lastUse=%d) */\n",
					freeFn, varName, analysis.ShapeString(shape), lastUse)
				// Track shape-based frees
				if g.stats != nil {
					switch shape {
					case analysis.ShapeTree:
						g.stats.ShapeTreeFrees++
					case analysis.ShapeDAG:
						g.stats.ShapeDAGFrees++
					}
				}
			}
		} else {
			freeFn = analysis.ShapeFreeStrategy(shape)
			rcOptComment = fmt.Sprintf("    %s(%s); /* ASAP Clean (shape: %s, lastUse=%d) */\n",
				freeFn, varName, analysis.ShapeString(shape), lastUse)
		}

		if freeFn == "" {
			sb.WriteString(rcOptComment)
			continue
		}

		if g.reuseCtx != nil {
			g.reuseCtx.Ctx.MarkFreed(varName, 0)
			if g.reuseCtx.Ctx.WillBeReused(varName) {
				sb.WriteString(fmt.Sprintf("    /* %s reused in outer scope - no free */\n", varName))
				continue
			}
			g.reuseCtx.Ctx.ConsumePendingFree(varName)
		}

		sb.WriteString(rcOptComment)
	}

	// Exit purity context if we entered one
	if g.enablePurity && g.purityCtx != nil && len(pureVars) > 0 {
		g.purityCtx.ExitPureContext(pureVars)
		sb.WriteString(fmt.Sprintf("    /* Pure context exit: %d read-only vars released */\n", len(pureVars)))
	}

	// Clean up tethered variables tracking
	for _, varName := range tetheredInScope {
		g.UntetherVar(varName)
	}

	// Exit region and add region info comment
	if g.regionCtx != nil && region != nil {
		g.regionCtx.ExitScope()
		if len(region.Allocations) > 0 {
			sb.WriteString(fmt.Sprintf("    /* Region %d (%s): %d allocations */\n",
				region.ID, region.Name, len(region.Allocations)))
			if g.stats != nil {
				g.stats.RegionsCreated++
			}
		}
	}

	sb.WriteString("    _res;\n})")

	return sb.String()
}

// GenerateProgram generates a complete C program
func (g *CodeGenerator) GenerateProgram(exprs []*ast.Value) {
	// Generate runtime
	runtime := NewRuntimeGenerator(g.w, g.registry)
	runtime.GenerateAll()

	// Generate main function
	g.emit("\nint main(void) {\n")
	g.emit("    Obj* result;\n")

	for _, expr := range exprs {
		if ast.IsCode(expr) {
			g.emit("    result = %s;\n", expr.Str)
		} else {
			g.emit("    result = %s;\n", g.ValueToCExpr(expr))
		}
		g.emit("    if (result) {\n")
		g.emit("        switch (result->tag) {\n")
		g.emit("        case TAG_INT:\n")
		g.emit("            printf(\"Result: %%ld\\n\", result->i);\n")
		g.emit("            break;\n")
		g.emit("        case TAG_FLOAT:\n")
		g.emit("            printf(\"Result: %%g\\n\", result->f);\n")
		g.emit("            break;\n")
		g.emit("        case TAG_CHAR:\n")
		g.emit("            printf(\"Result: %%c\\n\", (char)result->i);\n")
		g.emit("            break;\n")
		g.emit("        default:\n")
		g.emit("            /* Non-scalar result */\n")
		g.emit("            break;\n")
		g.emit("        }\n")
		g.emit("    }\n")
		g.emit("    free_obj(result);\n")
	}

	g.emit("    flush_freelist();\n")
	g.emit("    return 0;\n")
	g.emit("}\n")
}

// GenerateProgramToString generates a complete C program as a string
func GenerateProgramToString(exprs []*ast.Value) string {
	var sb strings.Builder
	gen := NewCodeGenerator(&sb)
	gen.GenerateProgram(exprs)
	return sb.String()
}

// GenerateProgram is a convenience wrapper for a single expression.
func GenerateProgram(expr *ast.Value) string {
	return GenerateProgramToString([]*ast.Value{expr})
}

// FreePoint represents a point where a variable should be freed
type FreePoint struct {
	VarName       string
	Point         int
	IsConditional bool
	FreeFn        string
}

// GenerateFreePlacement generates optimal free placement based on analysis
func GenerateFreePlacement(expr *ast.Value, vars []string) []FreePoint {
	escapeCtx := analysis.NewAnalysisContext()
	shapeCtx := analysis.NewShapeContext()

	for _, v := range vars {
		escapeCtx.AddVar(v)
	}

	escapeCtx.AnalyzeExpr(expr)
	escapeCtx.AnalyzeEscape(expr, analysis.EscapeGlobal)
	shapeCtx.AnalyzeShapes(expr)

	var freePoints []FreePoint
	for _, v := range vars {
		usage := escapeCtx.FindVar(v)
		shapeInfo := shapeCtx.FindShape(v)

		if usage == nil {
			continue
		}

		if usage.CapturedByLambda {
			continue // Don't free captured variables
		}

		if usage.Escape == analysis.EscapeGlobal {
			continue // Don't free escaping variables
		}

		shape := analysis.ShapeUnknown
		if shapeInfo != nil {
			shape = shapeInfo.Shape
		}

		freePoints = append(freePoints, FreePoint{
			VarName:       v,
			Point:         usage.LastUseDepth,
			IsConditional: false,
			FreeFn:        analysis.ShapeFreeStrategy(shape),
		})
	}

	return freePoints
}

// countVariableAccesses counts how many times each variable is accessed in an expression
// Used for scope tethering optimization - tethering is beneficial when a variable
// is accessed multiple times (amortizes tether/untether overhead)
func countVariableAccesses(expr *ast.Value) map[string]int {
	counts := make(map[string]int)
	countAccessesRec(expr, counts)
	return counts
}

func countAccessesRec(expr *ast.Value, counts map[string]int) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	switch expr.Tag {
	case ast.TSym:
		// Variable reference
		counts[expr.Str]++
	case ast.TCell:
		// Recursively count in subexpressions
		countAccessesRec(expr.Car, counts)
		countAccessesRec(expr.Cdr, counts)
	}
}
