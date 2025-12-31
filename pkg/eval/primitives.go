package eval

import (
	"fmt"
	"strings"

	"purple_go/pkg/ast"
)

// Symbol constants
var (
	SymT       = ast.NewSym("t")
	SymQuote   = ast.NewSym("quote")
	SymIf      = ast.NewSym("if")
	SymLambda  = ast.NewSym("lambda")
	SymLet     = ast.NewSym("let")
	SymLetrec  = ast.NewSym("letrec")
	SymAnd     = ast.NewSym("and")
	SymOr      = ast.NewSym("or")
	SymLift    = ast.NewSym("lift")
	SymRun     = ast.NewSym("run")
	SymEM      = ast.NewSym("EM")
	SymScan    = ast.NewSym("scan")
	SymGetMeta = ast.NewSym("get-meta")
	SymSetMeta = ast.NewSym("set-meta!")
)

// getTwoArgs safely extracts two arguments from a list
func getTwoArgs(args *ast.Value) (*ast.Value, *ast.Value, bool) {
	if ast.IsNil(args) || !ast.IsCell(args) {
		return nil, nil, false
	}
	a := args.Car
	rest := args.Cdr
	if ast.IsNil(rest) || !ast.IsCell(rest) {
		return nil, nil, false
	}
	b := rest.Car
	return a, b, true
}

// getOneArg safely extracts one argument from a list
func getOneArg(args *ast.Value) *ast.Value {
	if ast.IsNil(args) || !ast.IsCell(args) {
		return nil
	}
	return args.Car
}

// emitCCall generates a C function call for code values
func emitCCall(fnName string, a, b *ast.Value) *ast.Value {
	aStr := valueToCode(a)
	if b == nil || ast.IsNil(b) {
		return ast.NewCode(fmt.Sprintf("%s(%s)", fnName, aStr))
	}
	bStr := valueToCode(b)
	return ast.NewCode(fmt.Sprintf("%s(%s, %s)", fnName, aStr, bStr))
}

// valueToCode converts a value to its C code representation
func valueToCode(v *ast.Value) string {
	if v == nil {
		return "NULL"
	}
	if ast.IsCode(v) {
		return v.Str
	}
	if ast.IsInt(v) {
		return fmt.Sprintf("mk_int(%d)", v.Int)
	}
	if ast.IsNil(v) {
		return "NULL"
	}
	return v.String()
}

// PrimAdd implements + primitive (works with int and float)
func PrimAdd(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("add", a, b)
	}
	// Float arithmetic
	if ast.IsFloat(a) || ast.IsFloat(b) {
		af := toFloat(a)
		bf := toFloat(b)
		return ast.NewFloat(af + bf)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	return ast.NewInt(a.Int + b.Int)
}

// toFloat converts an int or float to float64
func toFloat(v *ast.Value) float64 {
	if ast.IsFloat(v) {
		return v.Float
	}
	if ast.IsInt(v) {
		return float64(v.Int)
	}
	return 0.0
}

// PrimSub implements - primitive (works with int and float)
func PrimSub(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("sub", a, b)
	}
	if ast.IsFloat(a) || ast.IsFloat(b) {
		return ast.NewFloat(toFloat(a) - toFloat(b))
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	return ast.NewInt(a.Int - b.Int)
}

// PrimMul implements * primitive (works with int and float)
func PrimMul(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("mul", a, b)
	}
	if ast.IsFloat(a) || ast.IsFloat(b) {
		return ast.NewFloat(toFloat(a) * toFloat(b))
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	return ast.NewInt(a.Int * b.Int)
}

// PrimDiv implements / primitive (works with int and float)
func PrimDiv(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("div_op", a, b)
	}
	if ast.IsFloat(a) || ast.IsFloat(b) {
		bf := toFloat(b)
		if bf == 0.0 {
			return ast.NewFloat(0.0)
		}
		return ast.NewFloat(toFloat(a) / bf)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if b.Int == 0 {
		return ast.NewInt(0)
	}
	return ast.NewInt(a.Int / b.Int)
}

// PrimMod implements % primitive
func PrimMod(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("mod_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if b.Int == 0 {
		return ast.NewInt(0)
	}
	return ast.NewInt(a.Int % b.Int)
}

// PrimEq implements = primitive
func PrimEq(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("eq_op", a, b)
	}
	if ast.IsInt(a) && ast.IsInt(b) {
		if a.Int == b.Int {
			return SymT
		}
		return ast.Nil
	}
	if ast.IsSym(a) && ast.IsSym(b) {
		if ast.SymEq(a, b) {
			return SymT
		}
		return ast.Nil
	}
	if ast.IsNil(a) && ast.IsNil(b) {
		return SymT
	}
	return ast.Nil
}

// PrimLt implements < primitive
func PrimLt(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("lt_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if a.Int < b.Int {
		return SymT
	}
	return ast.Nil
}

// PrimGt implements > primitive
func PrimGt(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("gt_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if a.Int > b.Int {
		return SymT
	}
	return ast.Nil
}

// PrimLe implements <= primitive
func PrimLe(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("le_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if a.Int <= b.Int {
		return SymT
	}
	return ast.Nil
}

// PrimGe implements >= primitive
func PrimGe(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("ge_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if a.Int >= b.Int {
		return SymT
	}
	return ast.Nil
}

// PrimNot implements not primitive
func PrimNot(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return SymT
	}
	if ast.IsCode(a) {
		return ast.NewCode(fmt.Sprintf("not_op(%s)", a.Str))
	}
	if ast.IsNil(a) {
		return SymT
	}
	return ast.Nil
}

// PrimCons implements cons primitive
func PrimCons(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("mk_pair", a, b)
	}
	return ast.NewCell(a, b)
}

// PrimCar implements car primitive
func PrimCar(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.Nil
	}
	if ast.IsCode(a) {
		return ast.NewCode(fmt.Sprintf("(%s)->a", a.Str))
	}
	if !ast.IsCell(a) {
		return ast.Nil
	}
	return a.Car
}

// PrimCdr implements cdr primitive
func PrimCdr(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.Nil
	}
	if ast.IsCode(a) {
		return ast.NewCode(fmt.Sprintf("(%s)->b", a.Str))
	}
	if !ast.IsCell(a) {
		return ast.Nil
	}
	return a.Cdr
}

// PrimFst is an alias for car
func PrimFst(args, menv *ast.Value) *ast.Value {
	return PrimCar(args, menv)
}

// PrimSnd is an alias for cdr
func PrimSnd(args, menv *ast.Value) *ast.Value {
	return PrimCdr(args, menv)
}

// PrimNull implements null? primitive
func PrimNull(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return SymT
	}
	if ast.IsCode(a) {
		return ast.NewCode(fmt.Sprintf("is_nil(%s)", a.Str))
	}
	if ast.IsNil(a) {
		return SymT
	}
	return ast.Nil
}

// PrimList implements list primitive
func PrimList(args, menv *ast.Value) *ast.Value {
	return args
}

// PrimLength implements length primitive
func PrimLength(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.NewInt(0)
	}
	return ast.NewInt(int64(ast.ListLen(a)))
}

// PrimAppend implements append primitive
func PrimAppend(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsNil(a) {
		return b
	}
	if !ast.IsCell(a) {
		return b
	}
	// Build new list by prepending elements of a to b
	items := ast.ListToSlice(a)
	result := b
	for i := len(items) - 1; i >= 0; i-- {
		result = ast.NewCell(items[i], result)
	}
	return result
}

// PrimReverse implements reverse primitive
func PrimReverse(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil || ast.IsNil(a) {
		return ast.Nil
	}
	result := ast.Nil
	for !ast.IsNil(a) && ast.IsCell(a) {
		result = ast.NewCell(a.Car, result)
		a = a.Cdr
	}
	return result
}

// PrimMap implements map primitive (higher-order)
// (map fn list) -> applies fn to each element
func PrimMap(args, menv *ast.Value) *ast.Value {
	fn := getOneArg(args)
	list := getOneArg(args.Cdr)
	if fn == nil || list == nil {
		return ast.Nil
	}

	var results []*ast.Value
	for !ast.IsNil(list) && ast.IsCell(list) {
		elem := list.Car
		// Apply fn to elem
		result := applyPrimFn(fn, ast.List1(elem), menv)
		results = append(results, result)
		list = list.Cdr
	}
	return ast.SliceToList(results)
}

// PrimFilter implements filter primitive
// (filter pred list) -> keeps elements where (pred elem) is true
func PrimFilter(args, menv *ast.Value) *ast.Value {
	pred := getOneArg(args)
	list := getOneArg(args.Cdr)
	if pred == nil || list == nil {
		return ast.Nil
	}

	var results []*ast.Value
	for !ast.IsNil(list) && ast.IsCell(list) {
		elem := list.Car
		// Apply pred to elem
		result := applyPrimFn(pred, ast.List1(elem), menv)
		if !ast.IsNil(result) && !(ast.IsInt(result) && result.Int == 0) {
			results = append(results, elem)
		}
		list = list.Cdr
	}
	return ast.SliceToList(results)
}

// PrimFold implements fold (right) primitive
// (fold fn init list) -> (fn e1 (fn e2 (fn e3 init)))
func PrimFold(args, menv *ast.Value) *ast.Value {
	fn := getOneArg(args)
	init := getOneArg(args.Cdr)
	list := getOneArg(args.Cdr.Cdr)
	if fn == nil {
		return ast.Nil
	}

	items := ast.ListToSlice(list)
	result := init
	for i := len(items) - 1; i >= 0; i-- {
		result = applyPrimFn(fn, ast.List2(items[i], result), menv)
	}
	return result
}

// PrimFoldl implements foldl (left) primitive
// (foldl fn init list) -> (fn (fn (fn init e1) e2) e3)
func PrimFoldl(args, menv *ast.Value) *ast.Value {
	fn := getOneArg(args)
	init := getOneArg(args.Cdr)
	list := getOneArg(args.Cdr.Cdr)
	if fn == nil {
		return ast.Nil
	}

	result := init
	for !ast.IsNil(list) && ast.IsCell(list) {
		result = applyPrimFn(fn, ast.List2(result, list.Car), menv)
		list = list.Cdr
	}
	return result
}

// PrimApply implements apply primitive
// (apply fn args-list) -> applies fn to the list of args
func PrimApply(args, menv *ast.Value) *ast.Value {
	fn := getOneArg(args)
	argList := getOneArg(args.Cdr)
	if fn == nil {
		return ast.Nil
	}
	return applyPrimFn(fn, argList, menv)
}

// PrimCompose implements compose primitive
// (compose f g) -> (lambda (x) (f (g x)))
func PrimCompose(args, menv *ast.Value) *ast.Value {
	f := getOneArg(args)
	g := getOneArg(args.Cdr)
	if f == nil || g == nil {
		return ast.Nil
	}
	// Return a new primitive that composes f and g
	return ast.NewPrim(func(innerArgs, innerMenv *ast.Value) *ast.Value {
		x := getOneArg(innerArgs)
		gResult := applyPrimFn(g, ast.List1(x), innerMenv)
		return applyPrimFn(f, ast.List1(gResult), innerMenv)
	})
}

// PrimFlip implements flip primitive
// (flip f) -> (lambda (x y) (f y x))
func PrimFlip(args, menv *ast.Value) *ast.Value {
	f := getOneArg(args)
	if f == nil {
		return ast.Nil
	}
	return ast.NewPrim(func(innerArgs, innerMenv *ast.Value) *ast.Value {
		x := getOneArg(innerArgs)
		y := getOneArg(innerArgs.Cdr)
		return applyPrimFn(f, ast.List2(y, x), innerMenv)
	})
}

// applyPrimFn applies a function (lambda or primitive) to arguments
func applyPrimFn(fn *ast.Value, args *ast.Value, menv *ast.Value) *ast.Value {
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

	return ast.Nil
}

// PrimPrint implements print primitive
func PrimPrint(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil {
		fmt.Println(a.String())
	}
	return ast.Nil
}

// PrimIsChar implements char? primitive
func PrimIsChar(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && ast.IsChar(a) {
		return SymT
	}
	return ast.Nil
}

// PrimCharToInt implements char->int primitive
func PrimCharToInt(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil || !ast.IsChar(a) {
		return ast.Nil
	}
	return ast.NewInt(a.Int)
}

// PrimIntToChar implements int->char primitive
func PrimIntToChar(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil || !ast.IsInt(a) {
		return ast.Nil
	}
	return ast.NewChar(rune(a.Int))
}

// PrimCharEq implements char=? primitive
func PrimCharEq(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsChar(a) && ast.IsChar(b) && a.Int == b.Int {
		return SymT
	}
	return ast.Nil
}

// PrimCharLt implements char<? primitive
func PrimCharLt(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsChar(a) && ast.IsChar(b) && a.Int < b.Int {
		return SymT
	}
	return ast.Nil
}

// Float-specific primitives

// PrimIsFloat implements float? primitive
func PrimIsFloat(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && ast.IsFloat(a) {
		return SymT
	}
	return ast.Nil
}

// PrimIntToFloat implements int->float primitive
func PrimIntToFloat(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.Nil
	}
	if ast.IsInt(a) {
		return ast.NewFloat(float64(a.Int))
	}
	if ast.IsFloat(a) {
		return a
	}
	return ast.Nil
}

// PrimFloatToInt implements float->int primitive (truncates)
func PrimFloatToInt(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.Nil
	}
	if ast.IsFloat(a) {
		return ast.NewInt(int64(a.Float))
	}
	if ast.IsInt(a) {
		return a
	}
	return ast.Nil
}

// PrimFloor implements floor primitive
func PrimFloor(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.Nil
	}
	if ast.IsFloat(a) {
		return ast.NewFloat(float64(int64(a.Float)))
	}
	if ast.IsInt(a) {
		return ast.NewFloat(float64(a.Int))
	}
	return ast.Nil
}

// PrimCeil implements ceil primitive
func PrimCeil(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.Nil
	}
	if ast.IsFloat(a) {
		f := a.Float
		if f == float64(int64(f)) {
			return ast.NewFloat(f)
		}
		if f > 0 {
			return ast.NewFloat(float64(int64(f) + 1))
		}
		return ast.NewFloat(float64(int64(f)))
	}
	if ast.IsInt(a) {
		return ast.NewFloat(float64(a.Int))
	}
	return ast.Nil
}

// PrimAbs implements abs primitive (works with int and float)
func PrimAbs(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.Nil
	}
	if ast.IsFloat(a) {
		f := a.Float
		if f < 0 {
			return ast.NewFloat(-f)
		}
		return a
	}
	if ast.IsInt(a) {
		i := a.Int
		if i < 0 {
			return ast.NewInt(-i)
		}
		return a
	}
	return ast.Nil
}

// isStringList checks if a value is a list of characters
func isStringList(v *ast.Value) bool {
	if ast.IsNil(v) {
		return true // empty string
	}
	for !ast.IsNil(v) && ast.IsCell(v) {
		if !ast.IsChar(v.Car) {
			return false
		}
		v = v.Cdr
	}
	return ast.IsNil(v)
}

// PrimIsString implements string? primitive
func PrimIsString(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && isStringList(a) {
		return SymT
	}
	return ast.Nil
}

// PrimStringToList implements string->list primitive (identity for char lists)
func PrimStringToList(args, menv *ast.Value) *ast.Value {
	return getOneArg(args)
}

// PrimListToString implements list->string primitive
// Converts list of chars to a displayable string symbol
func PrimListToString(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.NewSym("")
	}
	var sb strings.Builder
	for !ast.IsNil(a) && ast.IsCell(a) {
		if ast.IsChar(a.Car) {
			sb.WriteRune(rune(a.Car.Int))
		}
		a = a.Cdr
	}
	return ast.NewSym(sb.String())
}

// PrimStringLength implements string-length primitive
func PrimStringLength(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	return ast.NewInt(int64(ast.ListLen(a)))
}

// PrimStringAppend implements string-append primitive
func PrimStringAppend(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsNil(a) {
		return b
	}
	if !ast.IsCell(a) {
		return b
	}
	// Append a to b
	items := ast.ListToSlice(a)
	result := b
	for i := len(items) - 1; i >= 0; i-- {
		result = ast.NewCell(items[i], result)
	}
	return result
}

// PrimStringRef implements string-ref primitive (get char at index)
func PrimStringRef(args, menv *ast.Value) *ast.Value {
	str, idx, ok := getTwoArgs(args)
	if !ok || !ast.IsInt(idx) {
		return ast.Nil
	}
	i := int(idx.Int)
	for j := 0; !ast.IsNil(str) && ast.IsCell(str); j++ {
		if j == i {
			return str.Car
		}
		str = str.Cdr
	}
	return ast.Nil
}

// PrimSubstring implements substring primitive
func PrimSubstring(args, menv *ast.Value) *ast.Value {
	str := getOneArg(args)
	start := getOneArg(args.Cdr)
	end := getOneArg(args.Cdr.Cdr)
	if str == nil || !ast.IsInt(start) {
		return ast.Nil
	}

	startIdx := int(start.Int)
	endIdx := -1
	if ast.IsInt(end) {
		endIdx = int(end.Int)
	}

	var result []*ast.Value
	i := 0
	for !ast.IsNil(str) && ast.IsCell(str) {
		if i >= startIdx && (endIdx < 0 || i < endIdx) {
			result = append(result, str.Car)
		}
		i++
		str = str.Cdr
	}
	return ast.SliceToList(result)
}

// DefaultEnv creates the default environment with primitives
func DefaultEnv() *ast.Value {
	env := ast.Nil
	// Arithmetic
	env = EnvExtend(env, ast.NewSym("+"), ast.NewPrim(PrimAdd))
	env = EnvExtend(env, ast.NewSym("-"), ast.NewPrim(PrimSub))
	env = EnvExtend(env, ast.NewSym("*"), ast.NewPrim(PrimMul))
	env = EnvExtend(env, ast.NewSym("/"), ast.NewPrim(PrimDiv))
	env = EnvExtend(env, ast.NewSym("%"), ast.NewPrim(PrimMod))
	// Comparison
	env = EnvExtend(env, ast.NewSym("="), ast.NewPrim(PrimEq))
	env = EnvExtend(env, ast.NewSym("<"), ast.NewPrim(PrimLt))
	env = EnvExtend(env, ast.NewSym(">"), ast.NewPrim(PrimGt))
	env = EnvExtend(env, ast.NewSym("<="), ast.NewPrim(PrimLe))
	env = EnvExtend(env, ast.NewSym(">="), ast.NewPrim(PrimGe))
	// Logical
	env = EnvExtend(env, ast.NewSym("not"), ast.NewPrim(PrimNot))
	// List operations
	env = EnvExtend(env, ast.NewSym("cons"), ast.NewPrim(PrimCons))
	env = EnvExtend(env, ast.NewSym("car"), ast.NewPrim(PrimCar))
	env = EnvExtend(env, ast.NewSym("cdr"), ast.NewPrim(PrimCdr))
	env = EnvExtend(env, ast.NewSym("fst"), ast.NewPrim(PrimFst))
	env = EnvExtend(env, ast.NewSym("snd"), ast.NewPrim(PrimSnd))
	env = EnvExtend(env, ast.NewSym("null?"), ast.NewPrim(PrimNull))
	env = EnvExtend(env, ast.NewSym("list"), ast.NewPrim(PrimList))
	// Higher-order list operations
	env = EnvExtend(env, ast.NewSym("length"), ast.NewPrim(PrimLength))
	env = EnvExtend(env, ast.NewSym("append"), ast.NewPrim(PrimAppend))
	env = EnvExtend(env, ast.NewSym("reverse"), ast.NewPrim(PrimReverse))
	env = EnvExtend(env, ast.NewSym("map"), ast.NewPrim(PrimMap))
	env = EnvExtend(env, ast.NewSym("filter"), ast.NewPrim(PrimFilter))
	env = EnvExtend(env, ast.NewSym("fold"), ast.NewPrim(PrimFold))
	env = EnvExtend(env, ast.NewSym("foldr"), ast.NewPrim(PrimFold))
	env = EnvExtend(env, ast.NewSym("foldl"), ast.NewPrim(PrimFoldl))
	env = EnvExtend(env, ast.NewSym("apply"), ast.NewPrim(PrimApply))
	env = EnvExtend(env, ast.NewSym("compose"), ast.NewPrim(PrimCompose))
	env = EnvExtend(env, ast.NewSym("flip"), ast.NewPrim(PrimFlip))
	// Utility
	env = EnvExtend(env, ast.NewSym("print"), ast.NewPrim(PrimPrint))
	// Character operations
	env = EnvExtend(env, ast.NewSym("char?"), ast.NewPrim(PrimIsChar))
	env = EnvExtend(env, ast.NewSym("char->int"), ast.NewPrim(PrimCharToInt))
	env = EnvExtend(env, ast.NewSym("int->char"), ast.NewPrim(PrimIntToChar))
	env = EnvExtend(env, ast.NewSym("char=?"), ast.NewPrim(PrimCharEq))
	env = EnvExtend(env, ast.NewSym("char<?"), ast.NewPrim(PrimCharLt))
	// Float operations
	env = EnvExtend(env, ast.NewSym("float?"), ast.NewPrim(PrimIsFloat))
	env = EnvExtend(env, ast.NewSym("int->float"), ast.NewPrim(PrimIntToFloat))
	env = EnvExtend(env, ast.NewSym("float->int"), ast.NewPrim(PrimFloatToInt))
	env = EnvExtend(env, ast.NewSym("floor"), ast.NewPrim(PrimFloor))
	env = EnvExtend(env, ast.NewSym("ceil"), ast.NewPrim(PrimCeil))
	env = EnvExtend(env, ast.NewSym("abs"), ast.NewPrim(PrimAbs))
	// String operations
	env = EnvExtend(env, ast.NewSym("string?"), ast.NewPrim(PrimIsString))
	env = EnvExtend(env, ast.NewSym("string->list"), ast.NewPrim(PrimStringToList))
	env = EnvExtend(env, ast.NewSym("list->string"), ast.NewPrim(PrimListToString))
	env = EnvExtend(env, ast.NewSym("string-length"), ast.NewPrim(PrimStringLength))
	env = EnvExtend(env, ast.NewSym("string-append"), ast.NewPrim(PrimStringAppend))
	env = EnvExtend(env, ast.NewSym("string-ref"), ast.NewPrim(PrimStringRef))
	env = EnvExtend(env, ast.NewSym("substring"), ast.NewPrim(PrimSubstring))
	// Constants
	env = EnvExtend(env, ast.NewSym("t"), SymT)
	env = EnvExtend(env, ast.NewSym("nil"), ast.Nil)
	return env
}
