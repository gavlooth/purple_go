package analysis

import (
	"fmt"
	"purple_go/pkg/ast"
)

// ReuseCandidate represents a potential memory reuse opportunity
type ReuseCandidate struct {
	ID          int    // Unique identifier
	FreeVar     string // Variable being freed
	AllocVar    string // Variable being allocated
	FreeType    string // Type of freed object
	AllocType   string // Type of allocated object
	CanReuse    bool   // Whether sizes match
	SourceLine  int    // Location in source
	IsSameScope bool   // Whether both in same scope
}

// AllocationInfo tracks info about an allocation.
type AllocationInfo struct {
	VarName  string
	TypeName string
	Size     int
	Line     int
	IsFresh  bool
	IsFreed  bool
	FreeLine int
}

// ReusePattern represents a pattern of allocation followed by free
type ReusePattern int

const (
	ReuseNone    ReusePattern = iota // No reuse possible
	ReuseExact                       // Exact same size
	ReusePadded                      // Can reuse with padding
	ReusePartial                     // Can reuse part of memory
)

func (r ReusePattern) String() string {
	switch r {
	case ReuseExact:
		return "exact"
	case ReusePadded:
		return "padded"
	case ReusePartial:
		return "partial"
	default:
		return "none"
	}
}

// TypeSize maps types to their sizes (in words)
type TypeSize struct {
	Types map[string]int
}

// NewTypeSize creates a new type size map with defaults
func NewTypeSize() *TypeSize {
	ts := &TypeSize{
		Types: make(map[string]int),
	}
	// Default sizes (in 8-byte words)
	ts.Types["int"] = 2     // tag + value
	ts.Types["float"] = 2   // tag + value
	ts.Types["pair"] = 4    // tag + a + b + padding
	ts.Types["closure"] = 4 // tag + fn + env + arity
	ts.Types["string"] = 3  // tag + len + ptr
	ts.Types["box"] = 2     // tag + value
	ts.Types["Obj"] = 2     // generic size
	return ts
}

// GetSize returns the size of a type in words
func (ts *TypeSize) GetSize(typeName string) int {
	if size, ok := ts.Types[typeName]; ok {
		return size
	}
	return 2 // Default size
}

// CanReuse checks if a free'd type can be reused for an allocation
func (ts *TypeSize) CanReuse(freeType, allocType string) ReusePattern {
	freeSize := ts.GetSize(freeType)
	allocSize := ts.GetSize(allocType)

	if freeSize == allocSize {
		return ReuseExact
	} else if freeSize > allocSize {
		return ReusePadded
	}
	return ReuseNone
}

// ReuseContext tracks reuse opportunities
type ReuseContext struct {
	TypeSizes  *TypeSize
	Candidates []*ReuseCandidate
	NextID     int

	// Track pending frees and allocs
	PendingFrees []string          // Variables pending free
	PendingTypes map[string]string // Variable -> type mapping
	Reuses       map[string]string // Alloc var -> Free var mapping

	Allocations map[string]*AllocationInfo
}

// NewReuseContext creates a new reuse analysis context
func NewReuseContext() *ReuseContext {
	return &ReuseContext{
		TypeSizes:    NewTypeSize(),
		Candidates:   make([]*ReuseCandidate, 0),
		PendingFrees: make([]string, 0),
		PendingTypes: make(map[string]string),
		Reuses:       make(map[string]string),
		Allocations:  make(map[string]*AllocationInfo),
	}
}

// RegisterAllocation records an allocation for reuse analysis.
func (ctx *ReuseContext) RegisterAllocation(varName, typeName string, line int) {
	ctx.Allocations[varName] = &AllocationInfo{
		VarName:  varName,
		TypeName: typeName,
		Size:     ctx.TypeSizes.GetSize(typeName),
		Line:     line,
		IsFresh:  true,
	}
}

// MarkFreed marks an allocation as freed and pending reuse.
func (ctx *ReuseContext) MarkFreed(varName string, line int) {
	if info, ok := ctx.Allocations[varName]; ok {
		info.IsFreed = true
		info.FreeLine = line
		ctx.AddPendingFree(varName, info.TypeName)
		return
	}
	// Fallback if allocation not registered.
	ctx.AddPendingFree(varName, "Obj")
}

// ConsumePendingFree removes a pending free by name.
func (ctx *ReuseContext) ConsumePendingFree(name string) {
	for i := len(ctx.PendingFrees) - 1; i >= 0; i-- {
		if ctx.PendingFrees[i] == name {
			ctx.PendingFrees = append(ctx.PendingFrees[:i], ctx.PendingFrees[i+1:]...)
			delete(ctx.PendingTypes, name)
			return
		}
	}
}

// WillBeReused returns true if a free variable is scheduled for reuse.
func (ctx *ReuseContext) WillBeReused(varName string) bool {
	for _, freeVar := range ctx.Reuses {
		if freeVar == varName {
			return true
		}
	}
	return false
}

// FindBestReuse finds the best reuse candidate for a new allocation.
func (ctx *ReuseContext) FindBestReuse(typeName string, line int) *ReuseCandidate {
	targetSize := ctx.TypeSizes.GetSize(typeName)
	var best *ReuseCandidate
	bestWaste := int(^uint(0) >> 1)

	for i := len(ctx.PendingFrees) - 1; i >= 0; i-- {
		freeVar := ctx.PendingFrees[i]
		freeType := ctx.PendingTypes[freeVar]
		freeSize := ctx.TypeSizes.GetSize(freeType)

		if freeSize >= targetSize {
			waste := freeSize - targetSize
			if waste < bestWaste {
				best = &ReuseCandidate{
					ID:          ctx.NextID,
					FreeVar:     freeVar,
					FreeType:    freeType,
					AllocType:   typeName,
					CanReuse:    true,
					SourceLine:  line,
					IsSameScope: false,
				}
				bestWaste = waste
				if waste == 0 {
					break
				}
			}
		}
	}

	if best != nil {
		ctx.NextID++
		ctx.Candidates = append(ctx.Candidates, best)
	}
	return best
}

// AddPendingFree marks a variable as pending for free
func (ctx *ReuseContext) AddPendingFree(name, typeName string) {
	ctx.PendingFrees = append(ctx.PendingFrees, name)
	ctx.PendingTypes[name] = typeName
}

// TryReuse attempts to find a reuse candidate for an allocation
func (ctx *ReuseContext) TryReuse(allocVar, allocType string, line int) *ReuseCandidate {
	// Look through pending frees for a match
	for i := len(ctx.PendingFrees) - 1; i >= 0; i-- {
		freeVar := ctx.PendingFrees[i]
		freeType := ctx.PendingTypes[freeVar]

		pattern := ctx.TypeSizes.CanReuse(freeType, allocType)
		if pattern != ReuseNone {
			candidate := &ReuseCandidate{
				ID:          ctx.NextID,
				FreeVar:     freeVar,
				AllocVar:    allocVar,
				FreeType:    freeType,
				AllocType:   allocType,
				CanReuse:    true,
				SourceLine:  line,
				IsSameScope: true,
			}
			ctx.NextID++
			ctx.Candidates = append(ctx.Candidates, candidate)

			// Record the reuse
			ctx.Reuses[allocVar] = freeVar

			// Remove from pending frees
			ctx.PendingFrees = append(ctx.PendingFrees[:i], ctx.PendingFrees[i+1:]...)

			return candidate
		}
	}

	return nil
}

// GetReuse returns the free variable that can be reused for an allocation
func (ctx *ReuseContext) GetReuse(allocVar string) (string, bool) {
	freeVar, ok := ctx.Reuses[allocVar]
	return freeVar, ok
}

// ClearPendingFrees clears pending frees at scope exit
func (ctx *ReuseContext) ClearPendingFrees() {
	ctx.PendingFrees = ctx.PendingFrees[:0]
}

// ReuseAnalyzer analyzes code for reuse opportunities
type ReuseAnalyzer struct {
	Ctx       *ReuseContext
	ShapeCtx  *ShapeContext
	ScopeVars []map[string]string // Stack of scopes: var -> type
}

// NewReuseAnalyzer creates a new reuse analyzer
func NewReuseAnalyzer() *ReuseAnalyzer {
	return &ReuseAnalyzer{
		Ctx:       NewReuseContext(),
		ShapeCtx:  NewShapeContext(),
		ScopeVars: []map[string]string{make(map[string]string)},
	}
}

// PushScope enters a new scope
func (ra *ReuseAnalyzer) PushScope() {
	ra.ScopeVars = append(ra.ScopeVars, make(map[string]string))
}

// PopScope exits a scope, finding reuse opportunities
func (ra *ReuseAnalyzer) PopScope() {
	if len(ra.ScopeVars) > 1 {
		// Variables going out of scope become pending frees
		scope := ra.ScopeVars[len(ra.ScopeVars)-1]
		for varName, typeName := range scope {
			ra.Ctx.AddPendingFree(varName, typeName)
		}
		ra.ScopeVars = ra.ScopeVars[:len(ra.ScopeVars)-1]
	}
}

// AddVar adds a variable to current scope
func (ra *ReuseAnalyzer) AddVar(name, typeName string) {
	if len(ra.ScopeVars) > 0 {
		ra.ScopeVars[len(ra.ScopeVars)-1][name] = typeName
	}
}

// Analyze performs reuse analysis on an expression
func (ra *ReuseAnalyzer) Analyze(expr *ast.Value) {
	ra.analyzeExpr(expr, 0)
}

func (ra *ReuseAnalyzer) analyzeExpr(expr *ast.Value, line int) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	switch expr.Tag {
	case ast.TCell:
		if ast.IsSym(expr.Car) {
			sym := expr.Car.Str
			switch sym {
			case "let", "let*":
				ra.analyzeLetWithReuse(expr, line)

			case "cons":
				// Allocation - try to reuse
				if len(ra.ScopeVars) > 0 {
					ra.Ctx.TryReuse("_cons_result", "pair", line)
				}
				// Analyze arguments
				for args := expr.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					ra.analyzeExpr(args.Car, line)
				}

			case "box":
				ra.Ctx.TryReuse("_box_result", "box", line)
				for args := expr.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					ra.analyzeExpr(args.Car, line)
				}

			case "lambda":
				ra.Ctx.TryReuse("_closure_result", "closure", line)
				if expr.Cdr != nil && !ast.IsNil(expr.Cdr.Cdr) {
					ra.analyzeExpr(expr.Cdr.Cdr.Car, line)
				}

			case "if":
				// Analyze all branches
				for args := expr.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					ra.analyzeExpr(args.Car, line)
				}

			default:
				// Regular call - analyze arguments
				for args := expr.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					ra.analyzeExpr(args.Car, line)
				}
			}
		} else {
			// Application
			ra.analyzeExpr(expr.Car, line)
			for args := expr.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
				ra.analyzeExpr(args.Car, line)
			}
		}
	}
}

func (ra *ReuseAnalyzer) analyzeLetWithReuse(expr *ast.Value, line int) {
	if expr.Cdr == nil || ast.IsNil(expr.Cdr) {
		return
	}

	bindings := expr.Cdr.Car
	ra.PushScope()

	// Process bindings - try to reuse for each allocation
	for !ast.IsNil(bindings) && ast.IsCell(bindings) {
		binding := bindings.Car
		if ast.IsCell(binding) && ast.IsSym(binding.Car) {
			varName := binding.Car.Str
			typeName := ra.inferType(binding.Cdr)
			ra.AddVar(varName, typeName)

			// Try to find reuse for this binding
			ra.Ctx.TryReuse(varName, typeName, line)

			// Analyze init expression
			if binding.Cdr != nil && ast.IsCell(binding.Cdr) {
				ra.analyzeExpr(binding.Cdr.Car, line)
			}
		}
		bindings = bindings.Cdr
	}

	// Analyze body
	if expr.Cdr.Cdr != nil && ast.IsCell(expr.Cdr.Cdr) {
		ra.analyzeExpr(expr.Cdr.Cdr.Car, line)
	}

	ra.PopScope()
}

func (ra *ReuseAnalyzer) inferType(cdr *ast.Value) string {
	if cdr == nil || ast.IsNil(cdr) || !ast.IsCell(cdr) {
		return "Obj"
	}

	initExpr := cdr.Car
	if ast.IsInt(initExpr) {
		return "int"
	}
	if ast.IsFloat(initExpr) {
		return "float"
	}
	if ast.IsChar(initExpr) {
		return "char"
	}
	if ast.IsCell(initExpr) && ast.IsSym(initExpr.Car) {
		switch initExpr.Car.Str {
		case "cons", "list":
			return "pair"
		case "box":
			return "box"
		case "lambda":
			return "closure"
		}
	}
	return "Obj"
}

// GenerateReuseStats generates statistics about reuse opportunities
func (ra *ReuseAnalyzer) GenerateReuseStats() string {
	return fmt.Sprintf("/* Reuse Analysis: %d candidates found, %d reuses applied */\n",
		len(ra.Ctx.Candidates), len(ra.Ctx.Reuses))
}

// ShapeRouter routes allocations to appropriate strategies
type ShapeRouter struct {
	ShapeCtx *ShapeContext
}

// NewShapeRouter creates a new shape router
func NewShapeRouter(shapeCtx *ShapeContext) *ShapeRouter {
	return &ShapeRouter{ShapeCtx: shapeCtx}
}

// RouteStrategy determines the memory strategy for a variable
func (sr *ShapeRouter) RouteStrategy(varName string) string {
	shapeInfo := sr.ShapeCtx.FindShape(varName)
	shape := ShapeUnknown
	if shapeInfo != nil {
		shape = shapeInfo.Shape
	}

	switch shape {
	case ShapeTree:
		return "free_tree" // O(n) recursive free
	case ShapeDAG:
		return "dec_ref" // Reference counting
	case ShapeCyclic:
		return "arena_release" // Arena or weak refs
	default:
		return "dec_ref" // Safe default
	}
}

// GenerateShapeRoutedFree generates shape-specific free code
func (sr *ShapeRouter) GenerateShapeRoutedFree(varName string) string {
	strategy := sr.RouteStrategy(varName)
	shapeInfo := sr.ShapeCtx.FindShape(varName)
	shape := ShapeUnknown
	if shapeInfo != nil {
		shape = shapeInfo.Shape
	}

	return fmt.Sprintf(`/* Shape: %s -> Strategy: %s */
%s(%s);
`, ShapeString(shape), strategy, strategy, varName)
}

// PerceusOptimizer implements Perceus-style reuse
type PerceusOptimizer struct {
	ReuseCtx *ReuseContext
	TypeSize *TypeSize
}

// NewPerceusOptimizer creates a new Perceus optimizer
func NewPerceusOptimizer() *PerceusOptimizer {
	return &PerceusOptimizer{
		ReuseCtx: NewReuseContext(),
		TypeSize: NewTypeSize(),
	}
}

// GenerateReuse generates reuse code for an allocation
func (po *PerceusOptimizer) GenerateReuse(allocVar, allocType, freeVar, freeType string) string {
	pattern := po.TypeSize.CanReuse(freeType, allocType)

	switch pattern {
	case ReuseExact:
		return fmt.Sprintf(`/* Perceus: FBIP reuse %s -> %s (exact match) */
Obj* %s = reuse_as_%s(%s);
`, freeVar, allocVar, allocVar, allocType, freeVar)

	case ReusePadded:
		return fmt.Sprintf(`/* Perceus: FBIP reuse %s -> %s (padded, %d words unused) */
Obj* %s = reuse_as_%s_padded(%s);
`, freeVar, allocVar, po.TypeSize.GetSize(freeType)-po.TypeSize.GetSize(allocType),
			allocVar, allocType, freeVar)

	default:
		return fmt.Sprintf(`/* No reuse possible: %s (%s) cannot reuse %s (%s) */
Obj* %s = mk_%s();
`, allocVar, allocType, freeVar, freeType, allocVar, allocType)
	}
}

// GenerateReuseRuntime generates the reuse runtime support
func (po *PerceusOptimizer) GenerateReuseRuntime() string {
	return `/* ========== Perceus Reuse Runtime ========== */
/* FBIP: Functional But In-Place optimization */

/* Reuse a freed integer slot for a new integer */
static inline Obj* reuse_as_int(Obj* old, int64_t value) {
    if (old && old->mark == 1 && old->tag == TAG_INT) {
        /* In-place update */
        old->i = value;
        return old;
    }
    /* Fall back to fresh allocation */
    if (old) dec_ref(old);
    return mk_int(value);
}

/* Reuse a freed pair slot for a new pair */
static inline Obj* reuse_as_pair(Obj* old, Obj* a, Obj* b) {
    if (old && old->mark == 1 && old->is_pair) {
        /* In-place update */
        if (old->a) dec_ref(old->a);
        if (old->b) dec_ref(old->b);
        old->a = a;
        old->b = b;
        if (a) inc_ref(a);
        if (b) inc_ref(b);
        return old;
    }
    /* Fall back to fresh allocation */
    if (old) dec_ref(old);
    return mk_pair(a, b);
}

/* Reuse a freed box slot for a new box */
static inline Obj* reuse_as_box(Obj* old, Obj* value) {
    if (old && old->mark == 1 && old->tag == TAG_BOX) {
        /* In-place update */
        if (old->ptr) dec_ref((Obj*)old->ptr);
        old->ptr = value;
        if (value) inc_ref(value);
        return old;
    }
    /* Fall back to fresh allocation */
    if (old) dec_ref(old);
    return mk_box(value);
}

/* Check if a value can be reused (unique reference) */
static inline int can_reuse(Obj* obj) {
    return obj && obj->mark == 1;
}

/* Consume a value for potential reuse */
/* Returns the object if it can be reused, NULL otherwise */
static inline Obj* consume_for_reuse(Obj* obj) {
    if (can_reuse(obj)) {
        return obj;  /* Caller can reuse this memory */
    }
    if (obj) dec_ref(obj);  /* Free if not reusable */
    return NULL;
}
`
}

// DPSOptimizer implements Destination-Passing Style
type DPSOptimizer struct{}

// NewDPSOptimizer creates a new DPS optimizer
func NewDPSOptimizer() *DPSOptimizer {
	return &DPSOptimizer{}
}

// GenerateDPSRuntime generates DPS runtime support
func (dps *DPSOptimizer) GenerateDPSRuntime() string {
	return `/* ========== Destination-Passing Style Runtime ========== */
/* Pre-allocate destination and pass it down */

/* Map with destination passing - writes result into dest */
static void map_into(Obj** dest, Obj* (*f)(Obj*), Obj* xs) {
    if (!xs || !dest) return;

    Obj** current = dest;
    while (xs && xs->is_pair) {
        Obj* mapped = f(xs->a);

        /* Allocate directly into destination chain */
        *current = mk_pair(mapped, NULL);
        current = &((*current)->b);

        xs = xs->b;
    }
    *current = NULL;  /* Terminate list */
}

/* Filter with destination passing */
static void filter_into(Obj** dest, int (*pred)(Obj*), Obj* xs) {
    if (!xs || !dest) return;

    Obj** current = dest;
    while (xs && xs->is_pair) {
        if (pred(xs->a)) {
            *current = mk_pair(xs->a, NULL);
            inc_ref(xs->a);
            current = &((*current)->b);
        }
        xs = xs->b;
    }
    *current = NULL;
}

/* Append with destination passing - avoids intermediate allocations */
static void append_into(Obj** dest, Obj* xs, Obj* ys) {
    if (!dest) return;

    Obj** current = dest;

    /* Copy first list */
    while (xs && xs->is_pair) {
        *current = mk_pair(xs->a, NULL);
        inc_ref(xs->a);
        current = &((*current)->b);
        xs = xs->b;
    }

    /* Append second list (can share structure) */
    *current = ys;
    if (ys) inc_ref(ys);
}
`
}
