package analysis

import "purple_go/pkg/ast"

// Shape represents the shape classification of data structures
type Shape int

const (
	ShapeUnknown Shape = iota
	ShapeTree          // No sharing, no cycles - pure ASAP
	ShapeDAG           // Sharing but acyclic - refcount without cycle detection
	ShapeCyclic        // May have cycles - SCC RC or deferred
)

// ShapeInfo holds shape information for a variable
type ShapeInfo struct {
	VarName    string
	Shape      Shape
	Confidence int // 0-100
	AliasGroup int
}

// ShapeContext holds shape analysis state
type ShapeContext struct {
	Shapes         map[string]*ShapeInfo
	NextAliasGroup int
	ResultShape    Shape
	Changed        bool
}

// NewShapeContext creates a new shape analysis context
func NewShapeContext() *ShapeContext {
	return &ShapeContext{
		Shapes:         make(map[string]*ShapeInfo),
		NextAliasGroup: 1,
		ResultShape:    ShapeUnknown,
	}
}

// ShapeJoin computes the join (least upper bound) of two shapes
// Shape lattice: TREE < DAG < CYCLIC
func ShapeJoin(a, b Shape) Shape {
	if a == ShapeCyclic || b == ShapeCyclic {
		return ShapeCyclic
	}
	if a == ShapeDAG || b == ShapeDAG {
		return ShapeDAG
	}
	if a == ShapeTree || b == ShapeTree {
		return ShapeTree
	}
	return ShapeUnknown
}

// ShapeString returns the string representation of a shape
func ShapeString(s Shape) string {
	switch s {
	case ShapeTree:
		return "TREE"
	case ShapeDAG:
		return "DAG"
	case ShapeCyclic:
		return "CYCLIC"
	default:
		return "UNKNOWN"
	}
}

// FindShape looks up shape info for a variable
func (ctx *ShapeContext) FindShape(name string) *ShapeInfo {
	return ctx.Shapes[name]
}

// AddShape adds or updates shape info for a variable
func (ctx *ShapeContext) AddShape(name string, shape Shape) {
	if existing, ok := ctx.Shapes[name]; ok {
		joined := ShapeJoin(existing.Shape, shape)
		if joined != existing.Shape {
			existing.Shape = joined
			ctx.Changed = true
		}
		return
	}
	ctx.Shapes[name] = &ShapeInfo{
		VarName:    name,
		Shape:      shape,
		Confidence: 100,
		AliasGroup: ctx.NextAliasGroup,
	}
	ctx.NextAliasGroup++
}

// LookupShape gets the shape of an expression
func (ctx *ShapeContext) LookupShape(expr *ast.Value) Shape {
	if expr == nil {
		return ShapeUnknown
	}
	if ast.IsSym(expr) {
		if s := ctx.FindShape(expr.Str); s != nil {
			return s.Shape
		}
		return ShapeUnknown
	}
	// Literals are always TREE (no sharing)
	if ast.IsInt(expr) || ast.IsNil(expr) {
		return ShapeTree
	}
	return ShapeUnknown
}

// MayAlias checks if two expressions may alias
func (ctx *ShapeContext) MayAlias(a, b *ast.Value) bool {
	if a == nil || b == nil {
		return false
	}
	// Same variable definitely aliases
	if ast.IsSym(a) && ast.IsSym(b) && a.Str == b.Str {
		return true
	}
	// Different literals never alias
	if (ast.IsInt(a) || ast.IsNil(a)) && (ast.IsInt(b) || ast.IsNil(b)) {
		return false
	}
	// Check alias groups if both are variables
	if ast.IsSym(a) && ast.IsSym(b) {
		sa := ctx.FindShape(a.Str)
		sb := ctx.FindShape(b.Str)
		if sa != nil && sb != nil && sa.AliasGroup == sb.AliasGroup {
			return true
		}
	}
	// Conservative: assume may alias
	return true
}

// AnalyzeShapes analyzes an expression for shape information
func (ctx *ShapeContext) AnalyzeShapes(expr *ast.Value) {
	if expr == nil || ast.IsNil(expr) {
		ctx.ResultShape = ShapeTree
		return
	}

	switch expr.Tag {
	case ast.TInt, ast.TNil:
		ctx.ResultShape = ShapeTree

	case ast.TSym:
		if s := ctx.FindShape(expr.Str); s != nil {
			ctx.ResultShape = s.Shape
		} else {
			ctx.ResultShape = ShapeUnknown
		}

	case ast.TCell:
		op := expr.Car
		args := expr.Cdr

		if ast.IsSym(op) {
			switch op.Str {
			case "cons":
				carArg := args.Car
				cdrArg := args.Cdr.Car

				ctx.AnalyzeShapes(carArg)
				carShape := ctx.ResultShape

				ctx.AnalyzeShapes(cdrArg)
				cdrShape := ctx.ResultShape

				if carShape == ShapeTree && cdrShape == ShapeTree {
					if !ctx.MayAlias(carArg, cdrArg) {
						ctx.ResultShape = ShapeTree
					} else {
						ctx.ResultShape = ShapeDAG
					}
				} else {
					ctx.ResultShape = ShapeJoin(carShape, cdrShape)
					if ctx.ResultShape == ShapeTree {
						ctx.ResultShape = ShapeDAG
					}
				}
				return

			case "let":
				bindings := args.Car
				body := args.Cdr.Car

				for !ast.IsNil(bindings) && ast.IsCell(bindings) {
					bind := bindings.Car
					sym := bind.Car
					valExpr := bind.Cdr.Car

					ctx.AnalyzeShapes(valExpr)
					if ast.IsSym(sym) {
						ctx.AddShape(sym.Str, ctx.ResultShape)
					}
					bindings = bindings.Cdr
				}
				ctx.AnalyzeShapes(body)
				return

			case "letrec":
				bindings := args.Car
				body := args.Cdr.Car

				// Pre-mark all bound symbols as cyclic
				b := bindings
				for !ast.IsNil(b) && ast.IsCell(b) {
					bind := b.Car
					sym := bind.Car
					if ast.IsSym(sym) {
						ctx.AddShape(sym.Str, ShapeCyclic)
					}
					b = b.Cdr
				}

				// Analyze binding expressions
				b = bindings
				for !ast.IsNil(b) && ast.IsCell(b) {
					bind := b.Car
					sym := bind.Car
					valExpr := bind.Cdr.Car

					ctx.AnalyzeShapes(valExpr)
					if ast.IsSym(sym) {
						ctx.AddShape(sym.Str, ctx.ResultShape)
					}
					b = b.Cdr
				}
				ctx.AnalyzeShapes(body)
				return

			case "set!":
				target := args.Car
				if ast.IsSym(target) {
					ctx.AddShape(target.Str, ShapeCyclic)
				}
				ctx.ResultShape = ShapeCyclic
				return

			case "if":
				cond := args.Car
				thenBr := args.Cdr.Car
				var elseBr *ast.Value
				if !ast.IsNil(args.Cdr.Cdr) && ast.IsCell(args.Cdr.Cdr) {
					elseBr = args.Cdr.Cdr.Car
				}

				ctx.AnalyzeShapes(cond)
				ctx.AnalyzeShapes(thenBr)
				thenShape := ctx.ResultShape
				ctx.AnalyzeShapes(elseBr)
				elseShape := ctx.ResultShape

				ctx.ResultShape = ShapeJoin(thenShape, elseShape)
				return

			case "lambda":
				ctx.ResultShape = ShapeTree
				return

			case "lift":
				ctx.AnalyzeShapes(args.Car)
				return
			}
		}

		// Default: analyze all subexpressions and join
		result := ShapeUnknown
		ctx.AnalyzeShapes(op)
		result = ShapeJoin(result, ctx.ResultShape)
		for !ast.IsNil(args) && ast.IsCell(args) {
			ctx.AnalyzeShapes(args.Car)
			result = ShapeJoin(result, ctx.ResultShape)
			args = args.Cdr
		}
		if result == ShapeUnknown {
			result = ShapeDAG
		}
		ctx.ResultShape = result

	default:
		ctx.ResultShape = ShapeUnknown
	}
}

// ShapeFreeStrategy returns the free strategy for a given shape
func ShapeFreeStrategy(s Shape) string {
	switch s {
	case ShapeTree:
		return "free_tree"
	case ShapeDAG:
		return "dec_ref"
	case ShapeCyclic:
		return "deferred_release"
	default:
		return "dec_ref"
	}
}

// CyclicFreeStrategy represents the strategy for handling cyclic data
type CyclicFreeStrategy int

const (
	CyclicStrategyArena     CyclicFreeStrategy = iota // Arena fallback (opt-in for batch ops)
	CyclicStrategyDecRef                              // dec_ref (cycles broken by weak edges)
	CyclicStrategySCC                                 // SCC-based RC (for frozen cycles)
	CyclicStrategyDeferred                            // Deferred release
	CyclicStrategySymmetric                           // Symmetric RC (default for unbroken cycles)
)

// CyclicStrategyString returns string representation
func CyclicStrategyString(s CyclicFreeStrategy) string {
	switch s {
	case CyclicStrategyArena:
		return "arena"
	case CyclicStrategyDecRef:
		return "dec_ref"
	case CyclicStrategySCC:
		return "scc_release"
	case CyclicStrategyDeferred:
		return "deferred_release"
	case CyclicStrategySymmetric:
		return "symmetric_rc"
	default:
		return "symmetric_rc" // Default to symmetric for unbroken cycles
	}
}

// ShapeWithCycleInfo holds shape + cycle breaking info
type ShapeWithCycleInfo struct {
	Shape          Shape
	CyclesBroken   bool   // True if cycles are broken by auto-weak edges
	TypeName       string // The type name if known
	IsFrozen       bool   // True if data is immutable (can use SCC)
	Strategy       CyclicFreeStrategy
}

// DetermineStrategy determines the best memory strategy based on shape and cycle info
func (s *ShapeWithCycleInfo) DetermineStrategy() CyclicFreeStrategy {
	switch s.Shape {
	case ShapeTree:
		return CyclicStrategyDecRef // free_tree is actually used, but dec_ref is safe fallback
	case ShapeDAG:
		return CyclicStrategyDecRef
	case ShapeCyclic:
		if s.CyclesBroken {
			// Cycles are broken by auto-detected weak edges - dec_ref is safe
			return CyclicStrategyDecRef
		}
		if s.IsFrozen {
			// Immutable cyclic data - use SCC
			return CyclicStrategySCC
		}
		// Mutable cyclic data with unbroken cycles - use Symmetric RC
		// (More memory efficient than arena - frees immediately when orphaned)
		return CyclicStrategySymmetric
	default:
		return CyclicStrategySymmetric // Default to symmetric for unknown cyclic
	}
}

// GetFreeFunction returns the C function to call for freeing
func (s *ShapeWithCycleInfo) GetFreeFunction() string {
	switch s.DetermineStrategy() {
	case CyclicStrategyDecRef:
		if s.Shape == ShapeTree {
			return "free_tree"
		}
		return "dec_ref"
	case CyclicStrategySCC:
		return "scc_release"
	case CyclicStrategyArena:
		return "" // Arena uses bulk deallocation
	case CyclicStrategyDeferred:
		return "deferred_release"
	case CyclicStrategySymmetric:
		return "sym_exit_scope" // Symmetric RC releases on scope exit
	default:
		return "dec_ref"
	}
}
