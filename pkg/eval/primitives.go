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

	// Handle continuation application
	if ast.IsCont(fn) {
		arg := ast.Nil
		if !ast.IsNil(args) && ast.IsCell(args) {
			arg = args.Car
		}
		return fn.ContFn(arg)
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

		// Create body menv preserving handlers
		bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
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

		// Create body menv preserving handlers
		bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
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

// PrimDisplay implements display primitive (print without newline)
func PrimDisplay(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil {
		// For strings (list of chars), print the actual string
		if ast.IsCell(a) && !ast.IsNil(a) && ast.IsChar(a.Car) {
			for v := a; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
				if ast.IsChar(v.Car) {
					fmt.Printf("%c", rune(v.Car.Int))
				}
			}
		} else {
			fmt.Print(a.String())
		}
	}
	return ast.Nil
}

// PrimNewline implements newline primitive
func PrimNewline(args, menv *ast.Value) *ast.Value {
	fmt.Println()
	return ast.Nil
}

// PrimRead implements read primitive (reads from stdin)
// For now, reads a simple integer or symbol
func PrimRead(args, menv *ast.Value) *ast.Value {
	var input string
	_, err := fmt.Scan(&input)
	if err != nil {
		return ast.NewError("read: " + err.Error())
	}

	// Try to parse as integer
	var n int64
	if _, err := fmt.Sscanf(input, "%d", &n); err == nil {
		return ast.NewInt(n)
	}

	// Otherwise return as symbol
	return ast.NewSym(input)
}

// PrimReadChar implements read-char primitive
func PrimReadChar(args, menv *ast.Value) *ast.Value {
	var ch byte
	_, err := fmt.Scanf("%c", &ch)
	if err != nil {
		return ast.NewError("read-char: " + err.Error())
	}
	return ast.NewChar(rune(ch))
}

// PrimPeekChar implements peek-char primitive (returns EOF as nil)
// Note: This is a simplified version - true peek is complex in Go
func PrimPeekChar(args, menv *ast.Value) *ast.Value {
	return ast.NewError("peek-char: not implemented")
}

// PrimEof implements eof-object? primitive
func PrimEof(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && ast.IsError(a) && a.Str == "EOF" {
		return SymT
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

// Channel and Process primitives

// PrimMakeChan implements make-chan primitive
func PrimMakeChan(args, menv *ast.Value) *ast.Value {
	capacity := 0
	a := getOneArg(args)
	if a != nil && ast.IsInt(a) {
		capacity = int(a.Int)
	}
	return ast.NewChan(capacity)
}

// PrimChanSend implements chan-send! primitive (blocking)
func PrimChanSend(args, menv *ast.Value) *ast.Value {
	ch, val, ok := getTwoArgs(args)
	if !ok {
		return ast.NewError("chan-send!: requires channel and value")
	}
	if !ast.IsChan(ch) {
		return ast.NewError("chan-send!: first argument must be a channel")
	}
	ChanSendBlocking(ch, val)
	return val
}

// PrimChanRecv implements chan-recv! primitive (blocking)
func PrimChanRecv(args, menv *ast.Value) *ast.Value {
	ch := getOneArg(args)
	if ch == nil || !ast.IsChan(ch) {
		return ast.NewError("chan-recv!: requires a channel")
	}
	return ChanRecvBlocking(ch)
}

// PrimChanClose implements chan-close! primitive
func PrimChanClose(args, menv *ast.Value) *ast.Value {
	ch := getOneArg(args)
	if ch == nil || !ast.IsChan(ch) {
		return ast.NewError("chan-close!: requires a channel")
	}
	ChanClose(ch)
	return ast.Nil
}

// PrimIsChan implements chan? primitive
func PrimIsChan(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && ast.IsChan(a) {
		return SymT
	}
	return ast.Nil
}

// PrimIsProcess implements process? primitive
func PrimIsProcess(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && ast.IsProcess(a) {
		return SymT
	}
	return ast.Nil
}

// PrimIsThread implements thread? primitive
func PrimIsThread(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && ast.IsThread(a) {
		return SymT
	}
	return ast.Nil
}

// PrimIsAtom implements atom? primitive
func PrimIsAtom(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && ast.IsAtom(a) {
		return SymT
	}
	return ast.Nil
}

// PrimIsGreenChan implements green-chan? primitive
func PrimIsGreenChan(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && ast.IsGreenChan(a) {
		return SymT
	}
	return ast.Nil
}

// PrimProcessState implements process-state primitive
func PrimProcessState(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil || !ast.IsProcess(a) {
		return ast.NewError("process-state: requires a process")
	}
	states := []string{"ready", "running", "parked", "done"}
	if a.ProcState >= 0 && a.ProcState < len(states) {
		return ast.NewSym(states[a.ProcState])
	}
	return ast.NewSym("unknown")
}

// PrimProcessResult implements process-result primitive
func PrimProcessResult(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil || !ast.IsProcess(a) {
		return ast.NewError("process-result: requires a process")
	}
	if a.ProcState != ProcDone {
		return ast.Nil
	}
	return a.ProcResult
}

// Box (mutable cell) primitives

// PrimBox implements box primitive - creates a mutable reference cell
func PrimBox(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	return ast.NewBox(a)
}

// PrimUnbox implements unbox primitive - reads the value from a box
func PrimUnbox(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil || !ast.IsBox(a) {
		return ast.NewError("unbox: expected box")
	}
	return a.BoxValue
}

// PrimSetBox implements set-box! primitive - sets the value in a box
func PrimSetBox(args, menv *ast.Value) *ast.Value {
	box, val, ok := getTwoArgs(args)
	if !ok {
		return ast.NewError("set-box!: expected 2 arguments")
	}
	if !ast.IsBox(box) {
		return ast.NewError("set-box!: first argument must be a box")
	}
	box.BoxValue = val
	return val
}

// PrimIsBox implements box? primitive - checks if a value is a box
func PrimIsBox(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil && ast.IsBox(a) {
		return SymT
	}
	return ast.Nil
}

// PrimCtrTag implements ctr-tag primitive - returns the constructor name as a symbol
// (ctr-tag (cons 1 2)) → 'cell
// (ctr-tag 42) → 'int
// (ctr-tag (mk-Node 1 nil)) → 'Node
func PrimCtrTag(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.NewError("ctr-tag: requires 1 argument")
	}

	switch a.Tag {
	case ast.TInt:
		return ast.NewSym("int")
	case ast.TSym:
		return ast.NewSym("sym")
	case ast.TCell:
		return ast.NewSym("cell")
	case ast.TNil:
		return ast.NewSym("nil")
	case ast.TPrim:
		return ast.NewSym("prim")
	case ast.TLambda:
		return ast.NewSym("lambda")
	case ast.TRecLambda:
		return ast.NewSym("rec-lambda")
	case ast.TError:
		return ast.NewSym("error")
	case ast.TChar:
		return ast.NewSym("char")
	case ast.TFloat:
		return ast.NewSym("float")
	case ast.TMenv:
		return ast.NewSym("menv")
	case ast.TCode:
		return ast.NewSym("code")
	case ast.TBox:
		return ast.NewSym("box")
	case ast.TCont:
		return ast.NewSym("continuation")
	case ast.TChan:
		return ast.NewSym("channel")
	case ast.TProcess:
		return ast.NewSym("process")
	case ast.TUserType:
		return ast.NewSym(a.UserTypeName)
	default:
		return ast.NewSym("unknown")
	}
}

// PrimCtrArg implements ctr-arg primitive - returns the nth constructor argument
// (ctr-arg (cons 1 2) 0) → 1  (car)
// (ctr-arg (cons 1 2) 1) → 2  (cdr)
// (ctr-arg (mk-Node val next) 0) → val
func PrimCtrArg(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.NewError("ctr-arg: requires 2 arguments (value index)")
	}
	if !ast.IsInt(b) {
		return ast.NewError("ctr-arg: second argument must be an integer")
	}
	idx := int(b.Int)

	switch a.Tag {
	case ast.TCell:
		switch idx {
		case 0:
			return a.Car
		case 1:
			return a.Cdr
		default:
			return ast.NewError("ctr-arg: cell only has indices 0 and 1")
		}
	case ast.TBox:
		if idx == 0 {
			return a.BoxValue
		}
		return ast.NewError("ctr-arg: box only has index 0")
	case ast.TUserType:
		// Return field by index using the stored field order
		if a.UserTypeFieldOrder == nil || idx >= len(a.UserTypeFieldOrder) {
			return ast.NewError(fmt.Sprintf("ctr-arg: %s has no field at index %d", a.UserTypeName, idx))
		}
		fieldName := a.UserTypeFieldOrder[idx]
		if val, ok := a.UserTypeFields[fieldName]; ok {
			return val
		}
		return ast.NewError(fmt.Sprintf("ctr-arg: %s field %s not found", a.UserTypeName, fieldName))
	default:
		return ast.NewError(fmt.Sprintf("ctr-arg: cannot get argument from %s", ast.TagName(a.Tag)))
	}
}

// PrimReifyEnv implements reify-env primitive - returns the current environment as an assoc list
// (let ((x 1) (y 2)) (reify-env)) → ((y . 2) (x . 1) ...)
func PrimReifyEnv(args, menv *ast.Value) *ast.Value {
	// The environment is in the menv if available
	if menv != nil && menv.Tag == ast.TMenv && menv.Env != nil {
		return menv.Env
	}
	// Return global environment if no local environment
	return GetGlobalEnv()
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
	// I/O operations
	env = EnvExtend(env, ast.NewSym("print"), ast.NewPrim(PrimPrint))
	env = EnvExtend(env, ast.NewSym("display"), ast.NewPrim(PrimDisplay))
	env = EnvExtend(env, ast.NewSym("newline"), ast.NewPrim(PrimNewline))
	env = EnvExtend(env, ast.NewSym("read"), ast.NewPrim(PrimRead))
	env = EnvExtend(env, ast.NewSym("read-char"), ast.NewPrim(PrimReadChar))
	env = EnvExtend(env, ast.NewSym("eof-object?"), ast.NewPrim(PrimEof))
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
	// Box (mutable cell) operations
	env = EnvExtend(env, ast.NewSym("box"), ast.NewPrim(PrimBox))
	env = EnvExtend(env, ast.NewSym("unbox"), ast.NewPrim(PrimUnbox))
	env = EnvExtend(env, ast.NewSym("set-box!"), ast.NewPrim(PrimSetBox))
	env = EnvExtend(env, ast.NewSym("box?"), ast.NewPrim(PrimIsBox))
	// Channel operations
	env = EnvExtend(env, ast.NewSym("make-chan"), ast.NewPrim(PrimMakeChan))
	env = EnvExtend(env, ast.NewSym("chan-send!"), ast.NewPrim(PrimChanSend))
	env = EnvExtend(env, ast.NewSym("chan-recv!"), ast.NewPrim(PrimChanRecv))
	env = EnvExtend(env, ast.NewSym("chan-close!"), ast.NewPrim(PrimChanClose))
	env = EnvExtend(env, ast.NewSym("chan?"), ast.NewPrim(PrimIsChan))
	// Process operations
	env = EnvExtend(env, ast.NewSym("process?"), ast.NewPrim(PrimIsProcess))
	env = EnvExtend(env, ast.NewSym("process-state"), ast.NewPrim(PrimProcessState))
	env = EnvExtend(env, ast.NewSym("process-result"), ast.NewPrim(PrimProcessResult))
	// Thread/Atom/GreenChan predicates
	env = EnvExtend(env, ast.NewSym("thread?"), ast.NewPrim(PrimIsThread))
	env = EnvExtend(env, ast.NewSym("atom?"), ast.NewPrim(PrimIsAtom))
	env = EnvExtend(env, ast.NewSym("green-chan?"), ast.NewPrim(PrimIsGreenChan))
	// Introspection operations
	env = EnvExtend(env, ast.NewSym("ctr-tag"), ast.NewPrim(PrimCtrTag))
	env = EnvExtend(env, ast.NewSym("ctr-arg"), ast.NewPrim(PrimCtrArg))
	env = EnvExtend(env, ast.NewSym("reify-env"), ast.NewPrim(PrimReifyEnv))
	// Constants
	env = EnvExtend(env, ast.NewSym("t"), SymT)
	env = EnvExtend(env, ast.NewSym("nil"), ast.Nil)
	return env
}
