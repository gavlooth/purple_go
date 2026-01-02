package analysis

import (
	"fmt"
	"purple_go/pkg/ast"
)

// Additional ownership concepts for function summaries
// Note: OwnerConsumed is now defined in ownership.go
const (
	OwnerFresh OwnershipClass = iota + 100 // Newly allocated by callee
)

// ParamSummary describes how a function uses a parameter
type ParamSummary struct {
	Name       string         // Parameter name
	Ownership  OwnershipClass // How parameter is used
	Escapes    bool           // Does it escape to heap/closure?
	StoredIn   []string       // Fields/structures it's stored in
	ReturnPath bool           // Does it flow to return value?
}

// ReturnSummary describes the return value
type ReturnSummary struct {
	Ownership OwnershipClass // Ownership of return value
	FromParam string         // If transferred from a param, which one
	IsFresh   bool           // Is it a fresh allocation?
	Shape     Shape          // Shape of returned value
}

// SideEffect represents a side effect of a function
type SideEffect int

const (
	EffectNone        SideEffect = 0
	EffectAllocates   SideEffect = 1 << iota // Allocates memory
	EffectFrees                              // Frees memory
	EffectMutates                            // Mutates arguments
	EffectIO                                 // Performs I/O
	EffectThrows                             // May throw exception
	EffectConcurrent                         // Uses concurrency primitives
)

func (e SideEffect) String() string {
	var effects []string
	if e&EffectAllocates != 0 {
		effects = append(effects, "allocates")
	}
	if e&EffectFrees != 0 {
		effects = append(effects, "frees")
	}
	if e&EffectMutates != 0 {
		effects = append(effects, "mutates")
	}
	if e&EffectIO != 0 {
		effects = append(effects, "io")
	}
	if e&EffectThrows != 0 {
		effects = append(effects, "throws")
	}
	if e&EffectConcurrent != 0 {
		effects = append(effects, "concurrent")
	}
	if len(effects) == 0 {
		return "pure"
	}
	return fmt.Sprintf("%v", effects)
}

// FunctionSummary captures the memory behavior of a function
type FunctionSummary struct {
	Name        string                      // Function name
	Params      []*ParamSummary             // Parameter summaries
	Return      *ReturnSummary              // Return value summary
	Effects     SideEffect                  // Side effects
	Allocations int                         // Estimated allocation count (O(1), O(n), etc.)
	CallGraph   map[string]*FunctionSummary // Functions this calls
	IsRecursive bool                        // Does it call itself?
	IsPrimitive bool                        // Is it a built-in primitive?
}

// NewFunctionSummary creates a new function summary
func NewFunctionSummary(name string) *FunctionSummary {
	return &FunctionSummary{
		Name:      name,
		Params:    make([]*ParamSummary, 0),
		Return:    &ReturnSummary{Ownership: OwnerFresh},
		Effects:   EffectNone,
		CallGraph: make(map[string]*FunctionSummary),
	}
}

// AddParam adds a parameter summary
func (fs *FunctionSummary) AddParam(name string, ownership OwnershipClass) *ParamSummary {
	ps := &ParamSummary{
		Name:      name,
		Ownership: ownership,
	}
	fs.Params = append(fs.Params, ps)
	return ps
}

// SummaryRegistry stores function summaries for interprocedural analysis
type SummaryRegistry struct {
	Summaries map[string]*FunctionSummary
}

// NewSummaryRegistry creates a new summary registry
func NewSummaryRegistry() *SummaryRegistry {
	sr := &SummaryRegistry{
		Summaries: make(map[string]*FunctionSummary),
	}
	sr.InitPrimitiveSummaries()
	return sr
}

// Register adds or updates a function summary
func (sr *SummaryRegistry) Register(summary *FunctionSummary) {
	sr.Summaries[summary.Name] = summary
}

// Lookup finds a function summary
func (sr *SummaryRegistry) Lookup(name string) *FunctionSummary {
	return sr.Summaries[name]
}

// InitPrimitiveSummaries initializes summaries for built-in primitives
func (sr *SummaryRegistry) InitPrimitiveSummaries() {
	// cons: allocates fresh pair, borrows arguments
	cons := NewFunctionSummary("cons")
	cons.IsPrimitive = true
	cons.AddParam("a", OwnerBorrowed)
	cons.AddParam("b", OwnerBorrowed)
	cons.Return = &ReturnSummary{Ownership: OwnerFresh, IsFresh: true, Shape: ShapeTree}
	cons.Effects = EffectAllocates
	sr.Register(cons)

	// car: borrows argument, returns borrowed reference
	car := NewFunctionSummary("car")
	car.IsPrimitive = true
	car.AddParam("pair", OwnerBorrowed)
	car.Return = &ReturnSummary{Ownership: OwnerBorrowed, FromParam: "pair"}
	sr.Register(car)

	// cdr: borrows argument, returns borrowed reference
	cdr := NewFunctionSummary("cdr")
	cdr.IsPrimitive = true
	cdr.AddParam("pair", OwnerBorrowed)
	cdr.Return = &ReturnSummary{Ownership: OwnerBorrowed, FromParam: "pair"}
	sr.Register(cdr)

	// list: allocates fresh list, borrows elements
	list := NewFunctionSummary("list")
	list.IsPrimitive = true
	list.Return = &ReturnSummary{Ownership: OwnerFresh, IsFresh: true, Shape: ShapeTree}
	list.Effects = EffectAllocates
	sr.Register(list)

	// map: allocates fresh list, borrows function and input list
	mapFn := NewFunctionSummary("map")
	mapFn.IsPrimitive = true
	mapFn.AddParam("f", OwnerBorrowed)
	mapFn.AddParam("xs", OwnerBorrowed)
	mapFn.Return = &ReturnSummary{Ownership: OwnerFresh, IsFresh: true, Shape: ShapeTree}
	mapFn.Effects = EffectAllocates
	mapFn.Allocations = -1 // O(n)
	sr.Register(mapFn)

	// filter: allocates fresh list
	filter := NewFunctionSummary("filter")
	filter.IsPrimitive = true
	filter.AddParam("pred", OwnerBorrowed)
	filter.AddParam("xs", OwnerBorrowed)
	filter.Return = &ReturnSummary{Ownership: OwnerFresh, IsFresh: true, Shape: ShapeTree}
	filter.Effects = EffectAllocates
	sr.Register(filter)

	// fold/foldl/foldr: no allocation (unless f allocates)
	fold := NewFunctionSummary("fold")
	fold.IsPrimitive = true
	fold.AddParam("f", OwnerBorrowed)
	fold.AddParam("init", OwnerBorrowed)
	fold.AddParam("xs", OwnerBorrowed)
	fold.Return = &ReturnSummary{Ownership: OwnerBorrowed, FromParam: "init"}
	sr.Register(fold)

	// box: allocates fresh box
	box := NewFunctionSummary("box")
	box.IsPrimitive = true
	box.AddParam("v", OwnerBorrowed)
	box.Return = &ReturnSummary{Ownership: OwnerFresh, IsFresh: true}
	box.Effects = EffectAllocates
	sr.Register(box)

	// unbox: borrows box, returns borrowed content
	unbox := NewFunctionSummary("unbox")
	unbox.IsPrimitive = true
	unbox.AddParam("b", OwnerBorrowed)
	unbox.Return = &ReturnSummary{Ownership: OwnerBorrowed, FromParam: "b"}
	sr.Register(unbox)

	// set-box!: mutates box
	setBox := NewFunctionSummary("set-box!")
	setBox.IsPrimitive = true
	setBox.AddParam("b", OwnerBorrowed)
	setBox.AddParam("v", OwnerBorrowed)
	setBox.Effects = EffectMutates
	sr.Register(setBox)

	// display/print: I/O
	display := NewFunctionSummary("display")
	display.IsPrimitive = true
	display.AddParam("v", OwnerBorrowed)
	display.Effects = EffectIO
	sr.Register(display)

	print := NewFunctionSummary("print")
	print.IsPrimitive = true
	print.AddParam("v", OwnerBorrowed)
	print.Effects = EffectIO
	sr.Register(print)

	// make-chan: allocates channel
	makeChan := NewFunctionSummary("make-chan")
	makeChan.IsPrimitive = true
	makeChan.AddParam("capacity", OwnerBorrowed)
	makeChan.Return = &ReturnSummary{Ownership: OwnerFresh, IsFresh: true}
	makeChan.Effects = EffectAllocates | EffectConcurrent
	sr.Register(makeChan)

	// chan-send!: transfers ownership into channel
	chanSend := NewFunctionSummary("chan-send!")
	chanSend.IsPrimitive = true
	chanSend.AddParam("ch", OwnerBorrowed)
	chanSend.AddParam("v", OwnerConsumed) // Value is consumed!
	chanSend.Effects = EffectConcurrent
	sr.Register(chanSend)

	// chan-recv!: receives fresh ownership
	chanRecv := NewFunctionSummary("chan-recv!")
	chanRecv.IsPrimitive = true
	chanRecv.AddParam("ch", OwnerBorrowed)
	chanRecv.Return = &ReturnSummary{Ownership: OwnerFresh, IsFresh: true}
	chanRecv.Effects = EffectConcurrent
	sr.Register(chanRecv)

	// error: may throw
	errorFn := NewFunctionSummary("error")
	errorFn.IsPrimitive = true
	errorFn.AddParam("msg", OwnerBorrowed)
	errorFn.Effects = EffectThrows
	sr.Register(errorFn)
}

// SummaryAnalyzer computes function summaries
type SummaryAnalyzer struct {
	Registry  *SummaryRegistry
	EscapeCtx *AnalysisContext
}

// NewSummaryAnalyzer creates a new summary analyzer
func NewSummaryAnalyzer() *SummaryAnalyzer {
	return &SummaryAnalyzer{
		Registry:  NewSummaryRegistry(),
		EscapeCtx: NewAnalysisContext(),
	}
}

// AnalyzeFunction computes a summary for a function definition
func (sa *SummaryAnalyzer) AnalyzeFunction(name string, params *ast.Value, body *ast.Value) *FunctionSummary {
	summary := NewFunctionSummary(name)

	// Extract parameter names
	paramNames := extractParamNames(params)
	for _, pname := range paramNames {
		// Default to borrowed - will be refined by analysis
		summary.AddParam(pname, OwnerBorrowed)
	}

	// Analyze body for escape, allocations, effects
	sa.analyzeBody(body, summary)

	// Check for recursion
	if callsFunction(body, name) {
		summary.IsRecursive = true
	}

	sa.Registry.Register(summary)
	return summary
}

// analyzeBody analyzes a function body
func (sa *SummaryAnalyzer) analyzeBody(body *ast.Value, summary *FunctionSummary) {
	if body == nil || ast.IsNil(body) {
		return
	}

	switch body.Tag {
	case ast.TCell:
		if ast.IsSym(body.Car) {
			sym := body.Car.Str

			// Check for allocation primitives
			switch sym {
			case "cons", "list", "box", "make-chan":
				summary.Effects |= EffectAllocates
				if callee := sa.Registry.Lookup(sym); callee != nil {
					summary.CallGraph[sym] = callee
				}
				// Analyze arguments
				for args := body.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					sa.analyzeBody(args.Car, summary)
				}

			case "set!", "set-box!":
				summary.Effects |= EffectMutates
				if callee := sa.Registry.Lookup(sym); callee != nil {
					summary.CallGraph[sym] = callee
				}
				// Analyze arguments
				for args := body.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					sa.analyzeBody(args.Car, summary)
				}

			case "display", "print", "newline", "read":
				summary.Effects |= EffectIO
				if callee := sa.Registry.Lookup(sym); callee != nil {
					summary.CallGraph[sym] = callee
				}
				// Analyze arguments
				for args := body.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					sa.analyzeBody(args.Car, summary)
				}

			case "error", "throw":
				summary.Effects |= EffectThrows
				if callee := sa.Registry.Lookup(sym); callee != nil {
					summary.CallGraph[sym] = callee
				}
				// Analyze arguments
				for args := body.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					sa.analyzeBody(args.Car, summary)
				}

			case "go", "chan-send!", "chan-recv!", "select":
				summary.Effects |= EffectConcurrent
				if callee := sa.Registry.Lookup(sym); callee != nil {
					summary.CallGraph[sym] = callee
				}
				// Analyze arguments
				for args := body.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					sa.analyzeBody(args.Car, summary)
				}

			case "lambda":
				// Lambda captures - check what's captured
				if body.Cdr != nil && body.Cdr.Cdr != nil {
					lambdaBody := body.Cdr.Cdr.Car
					sa.analyzeBody(lambdaBody, summary)
				}

			case "let", "letrec":
				// Analyze bindings and body
				if body.Cdr != nil {
					bindings := body.Cdr.Car
					for !ast.IsNil(bindings) && ast.IsCell(bindings) {
						binding := bindings.Car
						if ast.IsCell(binding) && !ast.IsNil(binding.Cdr) {
							sa.analyzeBody(binding.Cdr.Car, summary)
						}
						bindings = bindings.Cdr
					}
					if body.Cdr.Cdr != nil {
						sa.analyzeBody(body.Cdr.Cdr.Car, summary)
					}
				}

			case "if":
				// Analyze all branches
				args := body.Cdr
				for !ast.IsNil(args) && ast.IsCell(args) {
					sa.analyzeBody(args.Car, summary)
					args = args.Cdr
				}

			default:
				// Function call - check if callee affects ownership
				calleeSummary := sa.Registry.Lookup(sym)
				if calleeSummary != nil {
					summary.Effects |= calleeSummary.Effects
					summary.CallGraph[sym] = calleeSummary
				}
				// Analyze arguments
				args := body.Cdr
				for !ast.IsNil(args) && ast.IsCell(args) {
					sa.analyzeBody(args.Car, summary)
					args = args.Cdr
				}
			}
		} else {
			// Application of non-symbol
			sa.analyzeBody(body.Car, summary)
			args := body.Cdr
			for !ast.IsNil(args) && ast.IsCell(args) {
				sa.analyzeBody(args.Car, summary)
				args = args.Cdr
			}
		}

	case ast.TSym:
		// Symbol reference - check if it's a parameter
		for _, param := range summary.Params {
			if param.Name == body.Str {
				// Parameter is used
				break
			}
		}
	}
}

// extractParamNames extracts parameter names from a params list
func extractParamNames(params *ast.Value) []string {
	var names []string
	for !ast.IsNil(params) && ast.IsCell(params) {
		if ast.IsSym(params.Car) {
			names = append(names, params.Car.Str)
		}
		params = params.Cdr
	}
	// Handle single symbol (not a list)
	if ast.IsSym(params) {
		names = append(names, params.Str)
	}
	return names
}

// callsFunction checks if an expression calls a specific function
func callsFunction(expr *ast.Value, name string) bool {
	if expr == nil || ast.IsNil(expr) {
		return false
	}

	if ast.IsCell(expr) {
		// Check if this is a call to the function
		if ast.IsSym(expr.Car) && expr.Car.Str == name {
			return true
		}
		// Check subexpressions
		if callsFunction(expr.Car, name) {
			return true
		}
		return callsFunction(expr.Cdr, name)
	}

	return false
}

// GetParamOwnership returns the ownership class for a parameter at a call site
func (sr *SummaryRegistry) GetParamOwnership(funcName string, paramIdx int) OwnershipClass {
	summary := sr.Lookup(funcName)
	if summary == nil {
		return OwnerBorrowed // Default to borrowed if unknown
	}
	if paramIdx >= len(summary.Params) {
		return OwnerBorrowed
	}
	return summary.Params[paramIdx].Ownership
}

// GetReturnOwnership returns the ownership class for a function's return value
func (sr *SummaryRegistry) GetReturnOwnership(funcName string) OwnershipClass {
	summary := sr.Lookup(funcName)
	if summary == nil {
		return OwnerFresh // Default to fresh if unknown
	}
	if summary.Return == nil {
		return OwnerFresh
	}
	return summary.Return.Ownership
}

// DoesAllocate returns true if the function allocates memory
func (sr *SummaryRegistry) DoesAllocate(funcName string) bool {
	summary := sr.Lookup(funcName)
	if summary == nil {
		return false
	}
	return summary.Effects&EffectAllocates != 0
}

// IsPure returns true if the function has no side effects
func (sr *SummaryRegistry) IsPure(funcName string) bool {
	summary := sr.Lookup(funcName)
	if summary == nil {
		return false
	}
	return summary.Effects == EffectNone
}
