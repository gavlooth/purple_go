package eval

import (
	"fmt"

	"purple_go/pkg/ast"
)

// Evaluator is the stage-polymorphic evaluator
type Evaluator struct {
	// CodeGen is set when we need code generation support
	CodeGen CodeGenerator
}

// CodeGenerator interface for code generation during evaluation
type CodeGenerator interface {
	LiftValue(v *ast.Value) *ast.Value
	ValueToCExpr(v *ast.Value) string
}

// DefaultCodeGen provides basic code generation
type DefaultCodeGen struct{}

func (d *DefaultCodeGen) LiftValue(v *ast.Value) *ast.Value {
	if v == nil || ast.IsNil(v) {
		return ast.NewCode("NULL")
	}
	switch v.Tag {
	case ast.TInt:
		return ast.NewCode(fmt.Sprintf("mk_int(%d)", v.Int))
	case ast.TFloat:
		return ast.NewCode(fmt.Sprintf("mk_float(%g)", v.Float))
	case ast.TSym:
		return ast.NewCode(fmt.Sprintf("mk_sym(\"%s\")", v.Str))
	case ast.TCode:
		return v
	case ast.TCell:
		carCode := d.LiftValue(v.Car)
		cdrCode := d.LiftValue(v.Cdr)
		return ast.NewCode(fmt.Sprintf("mk_pair(%s, %s)", carCode.Str, cdrCode.Str))
	default:
		return ast.NewCode("NULL")
	}
}

func (d *DefaultCodeGen) ValueToCExpr(v *ast.Value) string {
	if v == nil || ast.IsNil(v) {
		return "NULL"
	}
	if ast.IsCode(v) {
		return v.Str
	}
	if ast.IsInt(v) {
		return fmt.Sprintf("mk_int(%d)", v.Int)
	}
	if ast.IsFloat(v) {
		return fmt.Sprintf("mk_float(%g)", v.Float)
	}
	return "NULL"
}

// New creates a new evaluator with default code generator
func New() *Evaluator {
	return &Evaluator{
		CodeGen: &DefaultCodeGen{},
	}
}

// NewMenv creates a new meta-environment
func NewMenv(parent, env *ast.Value) *ast.Value {
	return ast.NewMenv(env, parent,
		defaultHApp,
		defaultHLet,
		defaultHIf,
		defaultHLit,
		defaultHVar,
	)
}

// Default handlers
func defaultHLit(exp, menv *ast.Value) *ast.Value {
	return exp
}

func defaultHVar(exp, menv *ast.Value) *ast.Value {
	v := EnvLookup(menv.Env, exp)
	if v == nil {
		fmt.Printf("Error: Unbound variable: %s\n", exp.Str)
		return ast.Nil
	}
	return v
}

func defaultHApp(exp, menv *ast.Value) *ast.Value {
	fExpr := exp.Car
	argsExpr := exp.Cdr

	fn := Eval(fExpr, menv)
	if fn == nil {
		return ast.Nil
	}

	args := evalList(argsExpr, menv)

	if ast.IsPrim(fn) {
		return fn.Prim(args, menv)
	}

	if ast.IsLambda(fn) {
		params := fn.Params
		body := fn.Body
		closureEnv := fn.LamEnv

		newEnv := closureEnv
		p := params
		a := args
		for !ast.IsNil(p) && !ast.IsNil(a) && ast.IsCell(p) && ast.IsCell(a) {
			newEnv = EnvExtend(newEnv, p.Car, a.Car)
			p = p.Cdr
			a = a.Cdr
		}

		bodyMenv := NewMenv(menv.Parent, newEnv)
		bodyMenv.HApp = menv.HApp
		bodyMenv.HLet = menv.HLet
		bodyMenv.HIf = menv.HIf
		bodyMenv.HLit = menv.HLit
		bodyMenv.HVar = menv.HVar

		return Eval(body, bodyMenv)
	}

	if ast.IsRecLambda(fn) {
		selfName := fn.SelfName
		params := fn.Params
		body := fn.Body
		closureEnv := fn.LamEnv

		// Extend environment with self-reference
		newEnv := EnvExtend(closureEnv, selfName, fn)

		p := params
		a := args
		for !ast.IsNil(p) && !ast.IsNil(a) && ast.IsCell(p) && ast.IsCell(a) {
			newEnv = EnvExtend(newEnv, p.Car, a.Car)
			p = p.Cdr
			a = a.Cdr
		}

		bodyMenv := NewMenv(menv.Parent, newEnv)
		bodyMenv.HApp = menv.HApp
		bodyMenv.HLet = menv.HLet
		bodyMenv.HIf = menv.HIf
		bodyMenv.HLit = menv.HLit
		bodyMenv.HVar = menv.HVar

		return Eval(body, bodyMenv)
	}

	fmt.Printf("Error: Not a function: %s\n", fn.String())
	return ast.Nil
}

func defaultHLet(exp, menv *ast.Value) *ast.Value {
	args := exp.Cdr
	bindings := args.Car
	body := args.Cdr.Car

	anyCode := false
	newEnv := menv.Env

	// Collect bindings
	var bindList []bindInfo

	b := bindings
	for !ast.IsNil(b) && ast.IsCell(b) {
		bind := b.Car
		sym := bind.Car
		valExpr := bind.Cdr.Car
		val := Eval(valExpr, menv)
		if val == nil {
			val = ast.Nil
		}
		if ast.IsCode(val) {
			anyCode = true
		}
		bindList = append(bindList, bindInfo{sym: sym, val: val})
		b = b.Cdr
	}

	if anyCode {
		// Code generation path - generate C code block
		return generateLetCode(bindList, body, menv)
	}

	// Interpretation path
	for _, bi := range bindList {
		newEnv = EnvExtend(newEnv, bi.sym, bi.val)
	}

	bodyMenv := NewMenv(menv.Parent, newEnv)
	bodyMenv.HApp = menv.HApp
	bodyMenv.HLet = menv.HLet
	bodyMenv.HIf = menv.HIf
	bodyMenv.HLit = menv.HLit
	bodyMenv.HVar = menv.HVar

	return Eval(body, bodyMenv)
}

func generateLetCode(bindings []bindInfo, body *ast.Value, menv *ast.Value) *ast.Value {
	// Simple code generation for let bindings
	var decls, frees string
	newEnv := menv.Env

	cg := &DefaultCodeGen{}

	for _, bi := range bindings {
		valStr := ""
		if ast.IsCode(bi.val) {
			valStr = bi.val.Str
		} else {
			valStr = cg.ValueToCExpr(bi.val)
		}
		decls += fmt.Sprintf("  Obj* %s = %s;\n", bi.sym.Str, valStr)
		frees = fmt.Sprintf("  free_obj(%s);\n", bi.sym.Str) + frees

		ref := ast.NewCode(bi.sym.Str)
		newEnv = EnvExtend(newEnv, bi.sym, ref)
	}

	bodyMenv := NewMenv(menv.Parent, newEnv)
	bodyMenv.HApp = menv.HApp
	bodyMenv.HLet = menv.HLet
	bodyMenv.HIf = menv.HIf
	bodyMenv.HLit = menv.HLit
	bodyMenv.HVar = menv.HVar

	res := Eval(body, bodyMenv)
	resStr := ""
	if ast.IsCode(res) {
		resStr = res.Str
	} else {
		resStr = cg.ValueToCExpr(res)
	}

	code := fmt.Sprintf("({\n%s  Obj* _res = %s;\n%s  _res;\n})", decls, resStr, frees)
	return ast.NewCode(code)
}

func defaultHIf(exp, menv *ast.Value) *ast.Value {
	args := exp.Cdr
	condExpr := args.Car
	thenExpr := args.Cdr.Car
	var elseExpr *ast.Value
	if !ast.IsNil(args.Cdr.Cdr) && ast.IsCell(args.Cdr.Cdr) {
		elseExpr = args.Cdr.Cdr.Car
	} else {
		elseExpr = ast.Nil
	}

	c := Eval(condExpr, menv)

	if ast.IsCode(c) {
		t := Eval(thenExpr, menv)
		e := Eval(elseExpr, menv)

		tStr := ""
		if ast.IsCode(t) {
			tStr = t.Str
		} else {
			tStr = (&DefaultCodeGen{}).ValueToCExpr(t)
		}

		eStr := ""
		if ast.IsCode(e) {
			eStr = e.Str
		} else {
			eStr = (&DefaultCodeGen{}).ValueToCExpr(e)
		}

		return ast.NewCode(fmt.Sprintf("((%s)->i ? (%s) : (%s))", c.Str, tStr, eStr))
	}

	if !ast.IsNil(c) {
		return Eval(thenExpr, menv)
	}
	return Eval(elseExpr, menv)
}

// bindInfo holds a binding for let expressions
type bindInfo struct {
	sym *ast.Value
	val *ast.Value
}

// evalList evaluates a list of expressions
func evalList(list, menv *ast.Value) *ast.Value {
	if ast.IsNil(list) {
		return ast.Nil
	}
	h := Eval(list.Car, menv)
	t := evalList(list.Cdr, menv)
	return ast.NewCell(h, t)
}

// Eval is the main evaluation function
func Eval(expr, menv *ast.Value) *ast.Value {
	if ast.IsNil(expr) {
		return ast.Nil
	}
	if menv == nil {
		return ast.Nil
	}

	switch expr.Tag {
	case ast.TInt:
		return menv.HLit(expr, menv)

	case ast.TFloat:
		return menv.HLit(expr, menv)

	case ast.TChar:
		return menv.HLit(expr, menv)

	case ast.TCode:
		return expr

	case ast.TSym:
		return menv.HVar(expr, menv)

	case ast.TCell:
		op := expr.Car
		args := expr.Cdr

		// Special forms
		if ast.SymEqStr(op, "quote") {
			return args.Car
		}

		if ast.SymEqStr(op, "lift") {
			v := Eval(args.Car, menv)
			return (&DefaultCodeGen{}).LiftValue(v)
		}

		if ast.SymEqStr(op, "if") {
			return menv.HIf(expr, menv)
		}

		if ast.SymEqStr(op, "let") {
			return menv.HLet(expr, menv)
		}

		if ast.SymEqStr(op, "letrec") {
			return evalLetrec(expr, menv)
		}

		if ast.SymEqStr(op, "and") {
			return evalAnd(args, menv)
		}

		if ast.SymEqStr(op, "or") {
			return evalOr(args, menv)
		}

		if ast.SymEqStr(op, "lambda") {
			params := args.Car
			body := args.Cdr.Car

			// Check for recursive lambda: (lambda self (params) body)
			// where self is a symbol and params is a list
			if ast.IsSym(params) && !ast.IsNil(args.Cdr) && ast.IsCell(args.Cdr) {
				selfName := params
				actualParams := args.Cdr.Car
				actualBody := args.Cdr.Cdr.Car
				return ast.NewRecLambda(selfName, actualParams, actualBody, menv.Env)
			}

			return ast.NewLambda(params, body, menv.Env)
		}

		if ast.SymEqStr(op, "match") {
			return EvalMatch(expr, menv)
		}

		if ast.SymEqStr(op, "do") {
			return evalDo(args, menv)
		}

		if ast.SymEqStr(op, "error") {
			msg := Eval(args.Car, menv)
			msgStr := "error"
			if ast.IsSym(msg) {
				msgStr = msg.Str
			} else if ast.IsInt(msg) {
				msgStr = fmt.Sprintf("%d", msg.Int)
			}
			return ast.NewError(msgStr)
		}

		if ast.SymEqStr(op, "try") {
			// (try expr handler) - handler is a lambda that takes error
			tryExpr := args.Car
			handler := Eval(args.Cdr.Car, menv)
			result := Eval(tryExpr, menv)
			if ast.IsError(result) {
				// Apply handler to error message
				errVal := ast.NewSym(result.Str)
				return applyFn(handler, ast.List1(errVal), menv)
			}
			return result
		}

		if ast.SymEqStr(op, "assert") {
			cond := Eval(args.Car, menv)
			if ast.IsNil(cond) || (ast.IsInt(cond) && cond.Int == 0) {
				msg := "assertion failed"
				if !ast.IsNil(args.Cdr) {
					msgVal := Eval(args.Cdr.Car, menv)
					if ast.IsSym(msgVal) {
						msg = msgVal.Str
					}
				}
				return ast.NewError(msg)
			}
			return SymT
		}

		if ast.SymEqStr(op, "EM") {
			// Escape to meta-level
			e := args.Car
			parent := menv.Parent
			if ast.IsNil(parent) {
				parent = NewMenv(ast.Nil, ast.Nil)
				menv.Parent = parent
			}
			return Eval(e, parent)
		}

		if ast.SymEqStr(op, "run") {
			// (run code) - execute code value at base level
			codeVal := Eval(args.Car, menv)
			if ast.IsCode(codeVal) {
				// In interpretation mode, we can't execute C code
				// Return the code value as-is
				return codeVal
			}
			// If it's an AST, evaluate it
			return Eval(codeVal, menv)
		}

		if ast.SymEqStr(op, "eval") {
			// (eval expr) - evaluate expression at runtime
			exprVal := Eval(args.Car, menv)
			return Eval(exprVal, menv)
		}

		if ast.SymEqStr(op, "quasiquote") {
			return evalQuasiquote(args.Car, menv)
		}

		if ast.SymEqStr(op, "sym-eq?") {
			a := Eval(args.Car, menv)
			b := Eval(args.Cdr.Car, menv)
			if ast.IsSym(a) && ast.IsSym(b) && a.Str == b.Str {
				return SymT
			}
			return ast.Nil
		}

		if ast.SymEqStr(op, "gensym") {
			// (gensym) or (gensym prefix)
			prefix := "g"
			if !ast.IsNil(args) {
				prefixVal := Eval(args.Car, menv)
				if ast.IsSym(prefixVal) {
					prefix = prefixVal.Str
				}
			}
			return ast.NewSym(fmt.Sprintf("%s%d", prefix, gensymCounter()))
		}

		if ast.SymEqStr(op, "trace") {
			// (trace expr) - evaluate and print result
			result := Eval(args.Car, menv)
			fmt.Printf("TRACE: %s\n", result.String())
			return result
		}

		if ast.SymEqStr(op, "scan") {
			typeSym := Eval(args.Car, menv)
			val := Eval(args.Cdr.Car, menv)
			if !ast.IsSym(typeSym) || val == nil {
				return ast.Nil
			}
			valStr := ""
			if ast.IsCode(val) {
				valStr = val.Str
			} else {
				valStr = val.String()
			}
			return ast.NewCode(fmt.Sprintf("scan_%s(%s); // ASAP Mark", typeSym.Str, valStr))
		}

		// FFI: (ffi "function_name" arg1 arg2 ...)
		// Generates C code that calls an external function
		if ast.SymEqStr(op, "ffi") {
			return evalFFI(args, menv)
		}

		// FFI declaration: (ffi-declare "ret_type" "func_name" "arg1_type" ...)
		// Registers an external function declaration
		if ast.SymEqStr(op, "ffi-declare") {
			return evalFFIDeclare(args, menv)
		}

		// Regular application
		return menv.HApp(expr, menv)
	}

	return ast.Nil
}

func evalLetrec(exp, menv *ast.Value) *ast.Value {
	args := exp.Cdr
	bindings := args.Car
	body := args.Cdr.Car

	// Placeholder for uninitialized bindings
	uninit := ast.NewPrim(nil)

	// First pass: extend env with placeholders
	newEnv := menv.Env
	b := bindings
	for !ast.IsNil(b) && ast.IsCell(b) {
		bind := b.Car
		sym := bind.Car
		newEnv = EnvExtend(newEnv, sym, uninit)
		b = b.Cdr
	}

	// Create new menv
	recMenv := NewMenv(menv.Parent, newEnv)
	recMenv.HApp = menv.HApp
	recMenv.HLet = menv.HLet
	recMenv.HIf = menv.HIf
	recMenv.HLit = menv.HLit
	recMenv.HVar = menv.HVar

	// Second pass: evaluate and update
	b = bindings
	for !ast.IsNil(b) && ast.IsCell(b) {
		bind := b.Car
		sym := bind.Car
		valExpr := bind.Cdr.Car
		val := Eval(valExpr, recMenv)

		// Update placeholder
		e := newEnv
		for !ast.IsNil(e) && ast.IsCell(e) {
			pair := e.Car
			if ast.SymEq(pair.Car, sym) {
				pair.Cdr = val
				break
			}
			e = e.Cdr
		}
		b = b.Cdr
	}

	return Eval(body, recMenv)
}

func evalAnd(args, menv *ast.Value) *ast.Value {
	result := SymT
	rest := args
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		result = Eval(rest.Car, menv)
		if ast.IsCode(result) {
			// Code level - generate && chain
			remaining := rest.Cdr
			for !ast.IsNil(remaining) && ast.IsCell(remaining) {
				next := Eval(remaining.Car, menv)
				nextStr := ""
				if ast.IsCode(next) {
					nextStr = next.Str
				} else {
					nextStr = next.String()
				}
				result = ast.NewCode(fmt.Sprintf("(%s && %s)", result.Str, nextStr))
				remaining = remaining.Cdr
			}
			return result
		}
		if ast.IsNil(result) {
			return ast.Nil
		}
		rest = rest.Cdr
	}
	return result
}

func evalOr(args, menv *ast.Value) *ast.Value {
	rest := args
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		result := Eval(rest.Car, menv)
		if ast.IsCode(result) {
			// Code level - generate || chain
			remaining := rest.Cdr
			for !ast.IsNil(remaining) && ast.IsCell(remaining) {
				next := Eval(remaining.Car, menv)
				nextStr := ""
				if ast.IsCode(next) {
					nextStr = next.Str
				} else {
					nextStr = next.String()
				}
				result = ast.NewCode(fmt.Sprintf("(%s || %s)", result.Str, nextStr))
				remaining = remaining.Cdr
			}
			return result
		}
		if !ast.IsNil(result) {
			return result
		}
		rest = rest.Cdr
	}
	return ast.Nil
}

// evalDo evaluates a sequence of expressions, returning the last
func evalDo(args, menv *ast.Value) *ast.Value {
	var result *ast.Value = ast.Nil
	rest := args
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		result = Eval(rest.Car, menv)
		rest = rest.Cdr
	}
	return result
}

// applyFn applies a function to arguments
func applyFn(fn *ast.Value, args *ast.Value, menv *ast.Value) *ast.Value {
	if fn == nil {
		return ast.Nil
	}

	if ast.IsPrim(fn) {
		return fn.Prim(args, menv)
	}

	if ast.IsLambda(fn) {
		params := fn.Params
		body := fn.Body
		closureEnv := fn.LamEnv

		newEnv := closureEnv
		p := params
		a := args
		for !ast.IsNil(p) && !ast.IsNil(a) && ast.IsCell(p) && ast.IsCell(a) {
			newEnv = EnvExtend(newEnv, p.Car, a.Car)
			p = p.Cdr
			a = a.Cdr
		}

		bodyMenv := NewMenv(menv.Parent, newEnv)
		bodyMenv.HApp = menv.HApp
		bodyMenv.HLet = menv.HLet
		bodyMenv.HIf = menv.HIf
		bodyMenv.HLit = menv.HLit
		bodyMenv.HVar = menv.HVar

		return Eval(body, bodyMenv)
	}

	if ast.IsRecLambda(fn) {
		selfName := fn.SelfName
		params := fn.Params
		body := fn.Body
		closureEnv := fn.LamEnv

		// Extend environment with self-reference
		newEnv := EnvExtend(closureEnv, selfName, fn)

		p := params
		a := args
		for !ast.IsNil(p) && !ast.IsNil(a) && ast.IsCell(p) && ast.IsCell(a) {
			newEnv = EnvExtend(newEnv, p.Car, a.Car)
			p = p.Cdr
			a = a.Cdr
		}

		bodyMenv := NewMenv(menv.Parent, newEnv)
		bodyMenv.HApp = menv.HApp
		bodyMenv.HLet = menv.HLet
		bodyMenv.HIf = menv.HIf
		bodyMenv.HLit = menv.HLit
		bodyMenv.HVar = menv.HVar

		return Eval(body, bodyMenv)
	}

	fmt.Printf("Error: Not a function in applyFn: %s\n", fn.String())
	return ast.Nil
}

// EvalString evaluates a string expression
func EvalString(input string, env *ast.Value) (*ast.Value, error) {
	// Import parser inline to avoid cycle
	// This is a simple integration point
	return nil, fmt.Errorf("use parser.ParseString and Eval separately")
}

// Run evaluates an expression with a default environment
func Run(expr *ast.Value) *ast.Value {
	env := DefaultEnv()
	menv := NewMenv(ast.Nil, env)
	return Eval(expr, menv)
}

// gensymCount is the global counter for gensym
var gensymCount int64 = 0

// gensymCounter returns the next gensym number
func gensymCounter() int64 {
	gensymCount++
	return gensymCount
}

// evalQuasiquote handles quasiquote, unquote, and unquote-splicing
func evalQuasiquote(expr *ast.Value, menv *ast.Value) *ast.Value {
	return evalQQ(expr, menv, 0)
}

func evalQQ(expr *ast.Value, menv *ast.Value, depth int) *ast.Value {
	if expr == nil || ast.IsNil(expr) {
		return ast.Nil
	}

	// Non-list values are returned as-is
	if !ast.IsCell(expr) {
		return expr
	}

	head := expr.Car

	// Check for unquote: ,x or (unquote x)
	if ast.SymEqStr(head, "unquote") || ast.SymEqStr(head, ",") {
		if depth == 0 {
			// Evaluate the unquoted expression
			return Eval(expr.Cdr.Car, menv)
		}
		// Nested quasiquote - decrease depth
		inner := evalQQ(expr.Cdr.Car, menv, depth-1)
		return ast.List2(ast.NewSym("unquote"), inner)
	}

	// Check for unquote-splicing: ,@x or (unquote-splicing x)
	if ast.SymEqStr(head, "unquote-splicing") || ast.SymEqStr(head, ",@") {
		if depth == 0 {
			// This should be handled in list context
			return Eval(expr.Cdr.Car, menv)
		}
		inner := evalQQ(expr.Cdr.Car, menv, depth-1)
		return ast.List2(ast.NewSym("unquote-splicing"), inner)
	}

	// Check for nested quasiquote
	if ast.SymEqStr(head, "quasiquote") || ast.SymEqStr(head, "`") {
		inner := evalQQ(expr.Cdr.Car, menv, depth+1)
		return ast.List2(ast.NewSym("quasiquote"), inner)
	}

	// Regular list - process each element
	return evalQQList(expr, menv, depth)
}

func evalQQList(list *ast.Value, menv *ast.Value, depth int) *ast.Value {
	if ast.IsNil(list) {
		return ast.Nil
	}
	if !ast.IsCell(list) {
		return list
	}

	head := list.Car

	// Check for unquote-splicing at this level
	if ast.IsCell(head) {
		spliceHead := head.Car
		if ast.SymEqStr(spliceHead, "unquote-splicing") || ast.SymEqStr(spliceHead, ",@") {
			if depth == 0 {
				// Evaluate and splice
				spliced := Eval(head.Cdr.Car, menv)
				rest := evalQQList(list.Cdr, menv, depth)
				return appendLists(spliced, rest)
			}
		}
	}

	// Process head and tail
	newHead := evalQQ(head, menv, depth)
	newTail := evalQQList(list.Cdr, menv, depth)
	return ast.NewCell(newHead, newTail)
}

func appendLists(a, b *ast.Value) *ast.Value {
	if ast.IsNil(a) {
		return b
	}
	if !ast.IsCell(a) {
		return b
	}
	items := ast.ListToSlice(a)
	result := b
	for i := len(items) - 1; i >= 0; i-- {
		result = ast.NewCell(items[i], result)
	}
	return result
}

// FFI Support
// ============

// FFIDecl represents an external function declaration
type FFIDecl struct {
	Name       string
	ReturnType string
	ArgTypes   []string
}

// Global FFI registry
var ffiDeclarations = make(map[string]*FFIDecl)

// RegisterFFI registers an external function declaration
func RegisterFFI(name, retType string, argTypes []string) {
	ffiDeclarations[name] = &FFIDecl{
		Name:       name,
		ReturnType: retType,
		ArgTypes:   argTypes,
	}
}

// GetFFIDeclarations returns all registered FFI declarations
func GetFFIDeclarations() map[string]*FFIDecl {
	return ffiDeclarations
}

// ClearFFIDeclarations clears all FFI declarations (for testing)
func ClearFFIDeclarations() {
	ffiDeclarations = make(map[string]*FFIDecl)
}

// evalFFI handles (ffi "function_name" arg1 arg2 ...)
// In interpretation mode, it cannot actually call C functions
// In code generation mode, it generates C code to call the function
func evalFFI(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.NewError("ffi requires function name")
	}

	// Get function name
	fnNameVal := Eval(args.Car, menv)
	fnName := ""
	if ast.IsSym(fnNameVal) {
		fnName = fnNameVal.Str
	} else if ast.IsCell(fnNameVal) && ast.IsChar(fnNameVal.Car) {
		// It's a string (list of chars), convert to string
		var sb string
		for v := fnNameVal; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
			if ast.IsChar(v.Car) {
				sb += string(rune(v.Car.Int))
			}
		}
		fnName = sb
	} else {
		return ast.NewError("ffi function name must be a symbol or string")
	}

	// Evaluate arguments
	var evaledArgs []*ast.Value
	rest := args.Cdr
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		arg := Eval(rest.Car, menv)
		evaledArgs = append(evaledArgs, arg)
		rest = rest.Cdr
	}

	// Check if any argument is Code (staging mode)
	anyCode := false
	for _, arg := range evaledArgs {
		if ast.IsCode(arg) {
			anyCode = true
			break
		}
	}

	// In staging/codegen mode, generate C code
	if anyCode {
		return generateFFICode(fnName, evaledArgs)
	}

	// In pure interpretation mode, we can only call certain built-in functions
	return evalFFIInterpret(fnName, evaledArgs)
}

// generateFFICode generates C code for an FFI call
func generateFFICode(fnName string, args []*ast.Value) *ast.Value {
	var argStrs []string
	for _, arg := range args {
		if ast.IsCode(arg) {
			argStrs = append(argStrs, arg.Str)
		} else if ast.IsInt(arg) {
			argStrs = append(argStrs, fmt.Sprintf("%d", arg.Int))
		} else if ast.IsFloat(arg) {
			argStrs = append(argStrs, fmt.Sprintf("%g", arg.Float))
		} else if ast.IsNil(arg) {
			argStrs = append(argStrs, "NULL")
		} else if ast.IsChar(arg) {
			argStrs = append(argStrs, fmt.Sprintf("'%c'", rune(arg.Int)))
		} else {
			argStrs = append(argStrs, "NULL")
		}
	}

	// Generate the C function call
	code := fmt.Sprintf("%s(%s)", fnName, joinStrings(argStrs, ", "))
	return ast.NewCode(code)
}

// evalFFIInterpret handles FFI calls in pure interpretation mode
// Only supports a limited set of built-in functions
func evalFFIInterpret(fnName string, args []*ast.Value) *ast.Value {
	switch fnName {
	case "puts":
		// (ffi "puts" str) - print string
		if len(args) > 0 {
			printValue(args[0])
			fmt.Println()
		}
		return ast.NewInt(0)

	case "putchar":
		// (ffi "putchar" ch) - print character
		if len(args) > 0 && ast.IsChar(args[0]) {
			fmt.Printf("%c", rune(args[0].Int))
		} else if len(args) > 0 && ast.IsInt(args[0]) {
			fmt.Printf("%c", rune(args[0].Int))
		}
		return ast.NewInt(0)

	case "getchar":
		// (ffi "getchar") - read character
		var ch byte
		fmt.Scanf("%c", &ch)
		return ast.NewChar(rune(ch))

	case "exit":
		// (ffi "exit" code) - exit with code (for interpretation, just return error)
		code := int64(0)
		if len(args) > 0 && ast.IsInt(args[0]) {
			code = args[0].Int
		}
		return ast.NewError(fmt.Sprintf("exit:%d", code))

	case "printf":
		// Simple printf support
		if len(args) > 0 {
			printValue(args[0])
			for _, arg := range args[1:] {
				fmt.Print(" ")
				printValue(arg)
			}
		}
		return ast.NewInt(0)

	default:
		return ast.NewError(fmt.Sprintf("ffi: unknown function in interpreter: %s", fnName))
	}
}

// printValue prints a value for FFI output
func printValue(v *ast.Value) {
	if v == nil || ast.IsNil(v) {
		return
	}
	if ast.IsInt(v) {
		fmt.Print(v.Int)
	} else if ast.IsChar(v) {
		fmt.Printf("%c", rune(v.Int))
	} else if ast.IsSym(v) {
		fmt.Print(v.Str)
	} else if ast.IsCell(v) {
		// Could be a string (list of chars)
		for ; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
			if ast.IsChar(v.Car) {
				fmt.Printf("%c", rune(v.Car.Int))
			}
		}
	}
}

// evalFFIDeclare handles (ffi-declare "ret_type" "func_name" "arg1_type" ...)
func evalFFIDeclare(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) || ast.IsNil(args.Cdr) {
		return ast.NewError("ffi-declare requires return type and function name")
	}

	// Get return type
	retTypeVal := Eval(args.Car, menv)
	retType := valueToString(retTypeVal)

	// Get function name
	fnNameVal := Eval(args.Cdr.Car, menv)
	fnName := valueToString(fnNameVal)

	// Get argument types
	var argTypes []string
	rest := args.Cdr.Cdr
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		argTypeVal := Eval(rest.Car, menv)
		argTypes = append(argTypes, valueToString(argTypeVal))
		rest = rest.Cdr
	}

	// Register the declaration
	RegisterFFI(fnName, retType, argTypes)

	// Return the function name as a symbol (can be used with ffi)
	return ast.NewSym(fnName)
}

// valueToString converts a value to a string for FFI declarations
func valueToString(v *ast.Value) string {
	if v == nil || ast.IsNil(v) {
		return ""
	}
	if ast.IsSym(v) {
		return v.Str
	}
	// Handle string (list of chars)
	if ast.IsCell(v) && ast.IsChar(v.Car) {
		var sb string
		for ; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
			if ast.IsChar(v.Car) {
				sb += string(rune(v.Car.Int))
			}
		}
		return sb
	}
	return v.String()
}

// joinStrings joins strings with a separator
func joinStrings(strs []string, sep string) string {
	if len(strs) == 0 {
		return ""
	}
	result := strs[0]
	for i := 1; i < len(strs); i++ {
		result += sep + strs[i]
	}
	return result
}

// GenerateFFIDeclarations generates C declarations for all registered FFI functions
func GenerateFFIDeclarations() string {
	var sb string
	sb += "/* FFI Declarations */\n"
	for _, decl := range ffiDeclarations {
		sb += fmt.Sprintf("extern %s %s(", decl.ReturnType, decl.Name)
		if len(decl.ArgTypes) == 0 {
			sb += "void"
		} else {
			for i, argType := range decl.ArgTypes {
				if i > 0 {
					sb += ", "
				}
				sb += argType
			}
		}
		sb += ");\n"
	}
	sb += "\n"
	return sb
}
