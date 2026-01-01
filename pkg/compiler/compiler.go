package compiler

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"purple_go/pkg/analysis"
	"purple_go/pkg/ast"
	"purple_go/pkg/codegen"
)

// CValue represents a compiled C expression and its ownership.
type CValue struct {
	Expr   string
	Owned  bool
	IsNil  bool
	IsTemp bool
}

// VarInfo tracks compiler metadata for a binding.
type VarInfo struct {
	CName           string
	Boxed           bool
	ReturnOwnership analysis.OwnershipClass
}

// Compiler performs direct AST -> C lowering.
type Compiler struct {
	gen          *codegen.CodeGenerator
	registry     *analysis.SummaryRegistry
	globals      map[string]VarInfo
	scopes       []map[string]VarInfo
	tempCounter  int
	funcDefs     []string
	globalDefs   []string
	globalInits  []string
	helperDefs   []string
	primClosures map[string]bool
	errors       []error
}

// New creates a new Compiler.
func New() *Compiler {
	gen := codegen.NewCodeGenerator(&bytes.Buffer{})
	return &Compiler{
		gen:      gen,
		registry: analysis.NewSummaryRegistry(),
		globals:  make(map[string]VarInfo),
		scopes:   []map[string]VarInfo{make(map[string]VarInfo)},
		primClosures: make(map[string]bool),
	}
}

// CompileProgram compiles AST expressions into a full C program.
func (c *Compiler) CompileProgram(exprs []*ast.Value) (string, error) {
	var sb strings.Builder

	var nonDefs []*ast.Value
	for _, expr := range exprs {
		if c.isDeftype(expr) {
			if err := c.handleDeftype(expr); err != nil {
				return "", err
			}
			continue
		}
		if c.isDefine(expr) {
			if err := c.handleDefine(expr); err != nil {
				return "", err
			}
			continue
		}
		nonDefs = append(nonDefs, expr)
	}

	// Compile all expressions first to collect lambdas and helper defs
	var compiledExprs []CValue
	for _, expr := range nonDefs {
		cv, err := c.compileExpr(expr)
		if err != nil {
			return "", err
		}
		compiledExprs = append(compiledExprs, cv)
	}

	runtime := codegen.GenerateRuntime(c.genRegistry())
	sb.WriteString(runtime)
	sb.WriteString("\n")

	for _, def := range c.helperDefs {
		sb.WriteString(def)
		sb.WriteString("\n")
	}

	for _, def := range c.globalDefs {
		sb.WriteString(def)
		sb.WriteString("\n")
	}

	for _, fn := range c.funcDefs {
		sb.WriteString(fn)
		sb.WriteString("\n")
	}

	sb.WriteString("int main(void) {\n")
	sb.WriteString("    Obj* result = NULL;\n")

	for _, init := range c.globalInits {
		sb.WriteString(init)
	}

	for _, cv := range compiledExprs {
		sb.WriteString(fmt.Sprintf("    result = %s;\n", cv.Expr))
		if cv.Owned {
			sb.WriteString("    /* result owned by current expr */\n")
		}
		sb.WriteString("    if (result) {\n")
		sb.WriteString("        switch (result->tag) {\n")
		sb.WriteString("        case TAG_INT:\n")
		sb.WriteString("            printf(\"Result: %ld\\n\", result->i);\n")
		sb.WriteString("            break;\n")
		sb.WriteString("        case TAG_FLOAT:\n")
		sb.WriteString("            printf(\"Result: %g\\n\", result->f);\n")
		sb.WriteString("            break;\n")
		sb.WriteString("        case TAG_CHAR:\n")
		sb.WriteString("            printf(\"Result: %c\\n\", (char)result->i);\n")
		sb.WriteString("            break;\n")
		sb.WriteString("        default:\n")
		sb.WriteString("            /* Non-scalar result */\n")
		sb.WriteString("            break;\n")
		sb.WriteString("        }\n")
		sb.WriteString("    }\n")
		sb.WriteString("    free_obj(result);\n")
	}

	sb.WriteString("    flush_freelist();\n")
	sb.WriteString("    return 0;\n")
	sb.WriteString("}\n")

	return sb.String(), nil
}

// CompileToBinary compiles expressions to a native binary.
func (c *Compiler) CompileToBinary(exprs []*ast.Value, output string) (string, error) {
	code, err := c.CompileProgram(exprs)
	if err != nil {
		return "", err
	}

	tmpDir, err := os.MkdirTemp("", "purple_native_")
	if err != nil {
		return "", err
	}
	defer os.RemoveAll(tmpDir)

	srcPath := filepath.Join(tmpDir, "main.c")
	if err := os.WriteFile(srcPath, []byte(code), 0644); err != nil {
		return "", err
	}

	outPath := output
	if outPath == "" {
		outPath = "a.out"
	}

	cmd := exec.Command("gcc", "-std=c99", "-pthread", "-O2", "-o", outPath, srcPath)
	if out, err := cmd.CombinedOutput(); err != nil {
		return "", fmt.Errorf("compile failed: %v\n%s", err, out)
	}

	return outPath, nil
}

func (c *Compiler) genRegistry() *codegen.TypeRegistry {
	return codegen.GlobalRegistry()
}

func (c *Compiler) isDefine(expr *ast.Value) bool {
	return ast.IsCell(expr) && ast.IsSym(expr.Car) && expr.Car.Str == "define"
}

func (c *Compiler) handleDefine(expr *ast.Value) error {
	args := expr.Cdr
	if args == nil || ast.IsNil(args) {
		return fmt.Errorf("define: missing arguments")
	}
	first := args.Car

	// (define (name args...) body)
	if ast.IsCell(first) {
		name := first.Car
		if !ast.IsSym(name) {
			return fmt.Errorf("define: function name must be symbol")
		}
		params := first.Cdr
		body := args.Cdr.Car
		lam := ast.List3(ast.NewSym("lambda"), params, body)
		return c.defineGlobal(name.Str, lam)
	}

	// (define name value)
	if !ast.IsSym(first) {
		return fmt.Errorf("define: invalid name")
	}
	if args.Cdr == nil || ast.IsNil(args.Cdr) {
		return fmt.Errorf("define: missing value")
	}
	val := args.Cdr.Car
	return c.defineGlobal(first.Str, val)
}

func (c *Compiler) defineGlobal(name string, val *ast.Value) error {
	cName := c.cIdent(name)
	if _, ok := c.globals[name]; ok {
		return fmt.Errorf("define: duplicate global %s", name)
	}
	info := VarInfo{CName: cName}
	if c.isLambdaExpr(val) {
		params := val.Cdr.Car
		body := val.Cdr.Cdr.Car
		paramSet := make(map[string]bool)
		for !ast.IsNil(params) && ast.IsCell(params) {
			if ast.IsSym(params.Car) {
				paramSet[params.Car.Str] = true
			}
			params = params.Cdr
		}
		info.ReturnOwnership = c.inferReturnOwnership(body, paramSet)
	}
	c.globals[name] = info
	c.globalDefs = append(c.globalDefs, fmt.Sprintf("static Obj* %s = NULL;", cName))

	cv, err := c.compileExpr(val)
	if err != nil {
		return err
	}
	c.globalInits = append(c.globalInits, fmt.Sprintf("    %s = %s;\n", cName, cv.Expr))
	return nil
}

// defineFunction removed; functions are defined as closures via defineGlobal.

func (c *Compiler) compileExpr(expr *ast.Value) (CValue, error) {
	if expr == nil || ast.IsNil(expr) {
		return CValue{Expr: "NULL", Owned: false, IsNil: true}, nil
	}

	switch expr.Tag {
	case ast.TInt:
		return CValue{Expr: fmt.Sprintf("mk_int(%d)", expr.Int), Owned: true}, nil
	case ast.TFloat:
		return CValue{Expr: fmt.Sprintf("mk_float(%f)", expr.Float), Owned: true}, nil
	case ast.TChar:
		return CValue{Expr: fmt.Sprintf("mk_int(%d)", expr.Int), Owned: true}, nil
	case ast.TSym:
		if expr.Str == "nil" {
			return CValue{Expr: "NULL", Owned: false, IsNil: true}, nil
		}
		if expr.Str == "t" {
			return CValue{Expr: "mk_int(1)", Owned: true}, nil
		}
		if info, ok := c.lookup(expr.Str); ok {
			if info.Boxed {
				return CValue{Expr: fmt.Sprintf("box_get(%s)", info.CName), Owned: false}, nil
			}
			return CValue{Expr: info.CName, Owned: false}, nil
		}
		if c.isPrimitive(expr.Str) {
			return CValue{Expr: c.primClosureExpr(expr.Str), Owned: true}, nil
		}
		return CValue{}, fmt.Errorf("unbound symbol: %s", expr.Str)
	case ast.TCell:
		return c.compileCall(expr)
	default:
		return CValue{}, fmt.Errorf("unsupported expression tag: %v", expr.Tag)
	}
}

func (c *Compiler) compileCall(expr *ast.Value) (CValue, error) {
	op := expr.Car
	args := expr.Cdr
	if ast.IsSym(op) {
		switch op.Str {
		case "if":
			return c.compileIf(args)
		case "do":
			return c.compileDo(args)
		case "let", "letrec":
			return c.compileLet(expr)
		case "lambda":
			return c.compileLambda(expr)
		case "set!":
			return c.compileSet(args)
		case "quote":
			return c.compileQuote(args.Car)
		case "and":
			return c.compileAnd(args)
		case "or":
			return c.compileOr(args)
		case "try":
			return c.compileTry(args)
		case "error":
			return c.compileError(args)
		}

		// Primitive or function call
		return c.compileApply(op.Str, args)
	}

	// Non-symbol application: evaluate operator and call as closure
	return c.compileNonSymbolApply(op, args)
}

func (c *Compiler) compileNonSymbolApply(op *ast.Value, args *ast.Value) (CValue, error) {
	// Compile the operator (should evaluate to a closure)
	opVal, err := c.compileExpr(op)
	if err != nil {
		return CValue{}, err
	}

	// Compile all arguments
	var argVals []CValue
	for !ast.IsNil(args) && ast.IsCell(args) {
		cv, err := c.compileExpr(args.Car)
		if err != nil {
			return CValue{}, err
		}
		argVals = append(argVals, cv)
		args = args.Cdr
	}

	var sb strings.Builder
	sb.WriteString("({\n")

	// Evaluate operator
	opName := c.newTemp()
	sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", opName, opVal.Expr))

	// Evaluate and store arguments
	var argNames []string
	for i, av := range argVals {
		argName := fmt.Sprintf("_arg%d_%d", i, c.tempCounter)
		c.tempCounter++
		sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", argName, av.Expr))
		argNames = append(argNames, argName)
	}

	// Build args array and call
	if len(argNames) > 0 {
		sb.WriteString(fmt.Sprintf("    Obj* _args[%d];\n", len(argNames)))
		for i, name := range argNames {
			sb.WriteString(fmt.Sprintf("    _args[%d] = %s;\n", i, name))
		}
		sb.WriteString(fmt.Sprintf("    Obj* _res = call_closure(%s, _args, %d);\n", opName, len(argNames)))
	} else {
		sb.WriteString(fmt.Sprintf("    Obj* _res = call_closure(%s, NULL, 0);\n", opName))
	}

	// Free operator if owned
	if opVal.Owned {
		sb.WriteString(fmt.Sprintf("    dec_ref(%s);\n", opName))
	}

	// Free arguments if owned
	for i, av := range argVals {
		if av.Owned {
			sb.WriteString(fmt.Sprintf("    dec_ref(%s);\n", argNames[i]))
		}
	}

	sb.WriteString("    _res;\n")
	sb.WriteString("})")

	return CValue{Expr: sb.String(), Owned: true}, nil
}

func (c *Compiler) compileIf(args *ast.Value) (CValue, error) {
	cond := args.Car
	thenExpr := args.Cdr.Car
	elseExpr := args.Cdr.Cdr.Car

	condVal, err := c.compileExpr(cond)
	if err != nil {
		return CValue{}, err
	}
	thenVal, err := c.compileExpr(thenExpr)
	if err != nil {
		return CValue{}, err
	}
	elseVal, err := c.compileExpr(elseExpr)
	if err != nil {
		return CValue{}, err
	}

	condName := c.newTemp()
	resName := c.newTemp()
	var sb strings.Builder
	sb.WriteString("({\n")
	sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", condName, condVal.Expr))
	sb.WriteString(fmt.Sprintf("    Obj* %s = NULL;\n", resName))
	sb.WriteString(fmt.Sprintf("    if (is_truthy(%s)) {\n", condName))
	sb.WriteString(fmt.Sprintf("        %s = %s;\n", resName, thenVal.Expr))
	sb.WriteString("    } else {\n")
	sb.WriteString(fmt.Sprintf("        %s = %s;\n", resName, elseVal.Expr))
	sb.WriteString("    }\n")
	if condVal.Owned {
		sb.WriteString(fmt.Sprintf("    dec_ref(%s);\n", condName))
	}
	sb.WriteString(fmt.Sprintf("    %s;\n", resName))
	sb.WriteString("})")

	return CValue{Expr: sb.String(), Owned: thenVal.Owned && elseVal.Owned}, nil
}

func (c *Compiler) compileDo(args *ast.Value) (CValue, error) {
	var sb strings.Builder
	sb.WriteString("({\n")
	var last CValue
	for !ast.IsNil(args) && ast.IsCell(args) {
		cv, err := c.compileExpr(args.Car)
		if err != nil {
			return CValue{}, err
		}
		tmp := c.newTemp()
		last = cv
		if args.Cdr != nil && !ast.IsNil(args.Cdr) {
			sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", tmp, cv.Expr))
			if cv.Owned {
				sb.WriteString(fmt.Sprintf("    dec_ref(%s);\n", tmp))
			}
		} else {
			sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", tmp, cv.Expr))
			sb.WriteString(fmt.Sprintf("    %s;\n", tmp))
		}
		args = args.Cdr
	}
	sb.WriteString("})")
	return CValue{Expr: sb.String(), Owned: last.Owned}, nil
}

func (c *Compiler) compileLet(expr *ast.Value) (CValue, error) {
	bindings := expr.Cdr.Car
	body := expr.Cdr.Cdr.Car

	// Prepare new scope
	local := make(map[string]VarInfo)
	var bindOrder []string
	var bindNames []string
	var bindExprs []CValue

	for !ast.IsNil(bindings) && ast.IsCell(bindings) {
		binding := bindings.Car
		if !ast.IsCell(binding) || !ast.IsSym(binding.Car) {
			return CValue{}, fmt.Errorf("let: invalid binding")
		}
		name := binding.Car.Str
		cName := c.cIdent(name)
		local[name] = VarInfo{CName: cName}
		bindOrder = append(bindOrder, cName)
		bindNames = append(bindNames, name)
		val := binding.Cdr.Car
		cv, err := c.compileExpr(val)
		if err != nil {
			return CValue{}, err
		}
		bindExprs = append(bindExprs, cv)
		bindings = bindings.Cdr
	}

	c.pushScope(local)
	bodyVal, err := c.compileExpr(body)
	c.popScope()
	if err != nil {
		return CValue{}, err
	}

	// Escape analysis for bindings
	escapeCtx := analysis.NewAnalysisContext()
	for name := range local {
		escapeCtx.AddVar(name)
	}
	escapeCtx.AnalyzeExpr(body)
	escapeCtx.AnalyzeEscape(body, analysis.EscapeGlobal)

	var sb strings.Builder
	sb.WriteString("({\n")
	for i, cName := range bindOrder {
		sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", cName, bindExprs[i].Expr))
	}
	sb.WriteString(fmt.Sprintf("    Obj* _res = %s;\n", bodyVal.Expr))

	for i := len(bindOrder) - 1; i >= 0; i-- {
		usage := escapeCtx.FindVar(bindNames[i])
		if usage != nil && (usage.CapturedByLambda || usage.Escape == analysis.EscapeGlobal) {
			sb.WriteString(fmt.Sprintf("    /* %s escapes - no free */\n", bindOrder[i]))
			continue
		}
		if bindExprs[i].Owned {
			sb.WriteString(fmt.Sprintf("    dec_ref(%s);\n", bindOrder[i]))
		}
	}

	sb.WriteString("    _res;\n")
	sb.WriteString("})")

	return CValue{Expr: sb.String(), Owned: bodyVal.Owned}, nil
}

func (c *Compiler) compileSet(args *ast.Value) (CValue, error) {
	if ast.IsNil(args) || !ast.IsCell(args) || args.Cdr == nil || ast.IsNil(args.Cdr) {
		return CValue{}, fmt.Errorf("set!: requires target and value")
	}
	if !ast.IsSym(args.Car) {
		return CValue{}, fmt.Errorf("set!: target must be symbol")
	}
	target := args.Car.Str
	info, ok := c.lookup(target)
	if !ok {
		return CValue{}, fmt.Errorf("set!: unknown variable %s", target)
	}
	val, err := c.compileExpr(args.Cdr.Car)
	if err != nil {
		return CValue{}, err
	}

	cTarget := info.CName
	var sb strings.Builder
	sb.WriteString("({\n")
	sb.WriteString(fmt.Sprintf("    Obj* _newv = %s;\n", val.Expr))
	sb.WriteString(fmt.Sprintf("    Obj* _oldv = %s;\n", cTarget))
	sb.WriteString(fmt.Sprintf("    %s = _newv;\n", cTarget))
	sb.WriteString("    if (_oldv) dec_ref(_oldv);\n")
	sb.WriteString("    _newv;\n")
	sb.WriteString("})")

	return CValue{Expr: sb.String(), Owned: val.Owned}, nil
}

func (c *Compiler) compileApply(fnName string, args *ast.Value) (CValue, error) {
	// Primitive summaries
	summary := c.registry.Lookup(fnName)

	var argVals []CValue
	for !ast.IsNil(args) && ast.IsCell(args) {
		cv, err := c.compileExpr(args.Car)
		if err != nil {
			return CValue{}, err
		}
		argVals = append(argVals, cv)
		args = args.Cdr
	}

	callExpr := c.emitCall(fnName, argVals, summary)
	if summary == nil || summary.Return == nil {
		return CValue{Expr: callExpr, Owned: true}, nil
	}
	owned := summary.Return.Ownership == analysis.OwnerFresh
	return CValue{Expr: callExpr, Owned: owned}, nil
}

func (c *Compiler) emitCall(fnName string, args []CValue, summary *analysis.FunctionSummary) string {
	var argExprs []string
	var temps []string
	var frees []string

	for i, arg := range args {
		name := fmt.Sprintf("_arg%d_%d", i, c.tempCounter)
		c.tempCounter++
		argExprs = append(argExprs, name)
		temps = append(temps, fmt.Sprintf("    Obj* %s = %s;\n", name, arg.Expr))

		if arg.Owned {
			// Default to borrowed params unless summary says consumed.
			consumed := false
			if summary != nil && i < len(summary.Params) {
				consumed = summary.Params[i].Ownership == analysis.OwnerConsumed
			}
			if !consumed {
				frees = append(frees, fmt.Sprintf("    dec_ref(%s);\n", name))
			}
		}
	}

	// Check if calling a user-defined closure
	if info, ok := c.globals[fnName]; ok {
		// Call as closure - need to build args array
		return c.emitClosureCall(info.CName, argExprs, temps, frees)
	}

	// Check if it's a local variable (closure)
	if info, ok := c.lookup(fnName); ok {
		if !c.isPrimitive(fnName) {
			return c.emitClosureCall(info.CName, argExprs, temps, frees)
		}
	}

	call := fmt.Sprintf("%s(%s)", c.resolveFn(fnName), strings.Join(argExprs, ", "))
	if len(temps) == 0 {
		return call
	}

	var sb strings.Builder
	sb.WriteString("({\n")
	for _, t := range temps {
		sb.WriteString(t)
	}
	sb.WriteString(fmt.Sprintf("    Obj* _res = %s;\n", call))
	for _, f := range frees {
		sb.WriteString(f)
	}
	sb.WriteString("    _res;\n")
	sb.WriteString("})")
	return sb.String()
}

func (c *Compiler) emitClosureCall(closureName string, argExprs, temps, frees []string) string {
	var sb strings.Builder
	sb.WriteString("({\n")
	for _, t := range temps {
		sb.WriteString(t)
	}

	if len(argExprs) > 0 {
		sb.WriteString(fmt.Sprintf("    Obj* _args[%d];\n", len(argExprs)))
		for i, arg := range argExprs {
			sb.WriteString(fmt.Sprintf("    _args[%d] = %s;\n", i, arg))
		}
		sb.WriteString(fmt.Sprintf("    Obj* _res = call_closure(%s, _args, %d);\n", closureName, len(argExprs)))
	} else {
		sb.WriteString(fmt.Sprintf("    Obj* _res = call_closure(%s, NULL, 0);\n", closureName))
	}

	for _, f := range frees {
		sb.WriteString(f)
	}
	sb.WriteString("    _res;\n")
	sb.WriteString("})")
	return sb.String()
}

func (c *Compiler) resolveFn(name string) string {
	if fn, ok := globalFuncs[name]; ok {
		return fn
	}
	// Check if it's a known primitive
	if cFn, ok := primitiveNames[name]; ok {
		return cFn
	}
	return name
}

func (c *Compiler) lookup(name string) (VarInfo, bool) {
	for i := len(c.scopes) - 1; i >= 0; i-- {
		if v, ok := c.scopes[i][name]; ok {
			return v, true
		}
	}
	if g, ok := c.globals[name]; ok {
		return g, true
	}
	return VarInfo{}, false
}

func (c *Compiler) pushScope(scope map[string]VarInfo) {
	c.scopes = append(c.scopes, scope)
}

func (c *Compiler) popScope() {
	if len(c.scopes) > 1 {
		c.scopes = c.scopes[:len(c.scopes)-1]
	}
}

func (c *Compiler) newTemp() string {
	c.tempCounter++
	return fmt.Sprintf("_t%d", c.tempCounter)
}

func (c *Compiler) cIdent(name string) string {
	name = strings.ReplaceAll(name, "-", "_")
	name = strings.ReplaceAll(name, "?", "_p")
	name = strings.ReplaceAll(name, "!", "_bang")
	return fmt.Sprintf("v_%s_%d", name, c.tempCounter+1)
}

// unmangle removed; bindings track original names directly.

// funcs maps function names to their C identifiers.
var globalFuncs = make(map[string]string)

func (c *Compiler) funcs() map[string]string {
	return globalFuncs
}

// isDeftype checks if expr is a deftype form.
func (c *Compiler) isDeftype(expr *ast.Value) bool {
	return ast.IsCell(expr) && ast.IsSym(expr.Car) && expr.Car.Str == "deftype"
}

// handleDeftype processes a deftype declaration.
func (c *Compiler) handleDeftype(expr *ast.Value) error {
	args := expr.Cdr
	if args == nil || ast.IsNil(args) || !ast.IsSym(args.Car) {
		return fmt.Errorf("deftype: expected type name")
	}
	typeName := args.Car.Str

	var fields []codegen.TypeField
	rest := args.Cdr
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		field := rest.Car
		if !ast.IsCell(field) {
			return fmt.Errorf("deftype: invalid field spec")
		}
		fieldName := field.Car
		if !ast.IsSym(fieldName) {
			return fmt.Errorf("deftype: field name must be symbol")
		}
		fieldType := field.Cdr.Car
		if !ast.IsSym(fieldType) {
			return fmt.Errorf("deftype: field type must be symbol")
		}

		tf := codegen.TypeField{
			Name:        fieldName.Str,
			Type:        fieldType.Str,
			IsScannable: true,
			Strength:    codegen.FieldStrong,
		}

		// Check for :weak annotation
		if field.Cdr.Cdr != nil && !ast.IsNil(field.Cdr.Cdr) {
			ann := field.Cdr.Cdr.Car
			if ast.IsSym(ann) && ann.Str == ":weak" {
				tf.Strength = codegen.FieldWeak
			}
		}

		fields = append(fields, tf)
		rest = rest.Cdr
	}

	// Register with global type registry
	reg := codegen.GlobalRegistry()
	reg.RegisterType(typeName, fields)
	reg.BuildOwnershipGraph()
	reg.AnalyzeBackEdges()

	return nil
}

// isLambdaExpr checks if an expression is a lambda form.
func (c *Compiler) isLambdaExpr(expr *ast.Value) bool {
	return ast.IsCell(expr) && ast.IsSym(expr.Car) && expr.Car.Str == "lambda"
}

// inferReturnOwnership infers the ownership class of a function's return value.
func (c *Compiler) inferReturnOwnership(body *ast.Value, params map[string]bool) analysis.OwnershipClass {
	// Simple heuristic: if body is a parameter, it's borrowed
	// If body is a constructor call (cons, mk_*), it's fresh
	if ast.IsSym(body) {
		if params[body.Str] {
			return analysis.OwnerBorrowed
		}
	}
	if ast.IsCell(body) && ast.IsSym(body.Car) {
		switch body.Car.Str {
		case "cons", "list", "box":
			return analysis.OwnerFresh
		}
	}
	// Default: assume fresh (conservative)
	return analysis.OwnerFresh
}

// primitiveNames lists all built-in primitives that compile to C functions.
var primitiveNames = map[string]string{
	"+": "prim_add", "-": "prim_sub", "*": "prim_mul", "/": "prim_div",
	"%": "prim_mod", "<": "prim_lt", ">": "prim_gt", "<=": "prim_le",
	">=": "prim_ge", "=": "prim_eq", "eq?": "prim_eq",
	"cons": "mk_pair", "car": "obj_car", "cdr": "obj_cdr",
	"null?": "prim_null", "pair?": "prim_pair", "int?": "prim_int",
	"float?": "prim_float", "char?": "prim_char", "symbol?": "prim_sym",
	"box": "mk_box", "unbox": "box_get", "set-box!": "box_set",
	"not": "prim_not", "abs": "prim_abs",
	"display": "prim_display", "newline": "prim_newline", "print": "prim_print",
	"length": "list_length", "append": "list_append", "reverse": "list_reverse",
	"map": "list_map", "filter": "list_filter", "fold": "list_fold",
	"make-chan": "channel_create", "chan-send!": "channel_send", "chan-recv!": "channel_recv",
}

// isPrimitive checks if a name refers to a built-in primitive.
func (c *Compiler) isPrimitive(name string) bool {
	_, ok := primitiveNames[name]
	return ok
}

// primClosureExpr generates a closure wrapping a primitive.
func (c *Compiler) primClosureExpr(name string) string {
	if _, exists := c.primClosures[name]; !exists {
		c.primClosures[name] = true
		cName := c.primFuncName(name)
		wrapper := c.genPrimWrapper(name, cName)
		c.helperDefs = append(c.helperDefs, wrapper)
	}
	return fmt.Sprintf("mk_closure(%s_wrapper, NULL, NULL, 0, -1)", c.primFuncName(name))
}

func (c *Compiler) primFuncName(name string) string {
	if fn, ok := primitiveNames[name]; ok {
		return fn
	}
	return c.cIdent(name)
}

// Primitive arity map for wrapper generation
var primitiveArity = map[string]int{
	"+": 2, "-": 2, "*": 2, "/": 2, "%": 2,
	"<": 2, ">": 2, "<=": 2, ">=": 2, "=": 2, "eq?": 2,
	"cons": 2, "set-box!": 2,
	"car": 1, "cdr": 1, "null?": 1, "pair?": 1, "int?": 1,
	"float?": 1, "char?": 1, "symbol?": 1, "box": 1, "unbox": 1,
	"not": 1, "abs": 1, "display": 1, "print": 1, "newline": 0,
}

func (c *Compiler) genPrimWrapper(name, cFn string) string {
	wrapperName := cFn + "_wrapper"
	arity, ok := primitiveArity[name]
	if !ok {
		arity = 2 // Default to binary
	}

	switch arity {
	case 0:
		return fmt.Sprintf(`static Obj* %s(Obj** captures, Obj** args, int n) {
    (void)captures; (void)args; (void)n;
    return %s();
}
`, wrapperName, cFn)
	case 1:
		return fmt.Sprintf(`static Obj* %s(Obj** captures, Obj** args, int n) {
    (void)captures;
    if (n < 1) return NULL;
    return %s(args[0]);
}
`, wrapperName, cFn)
	default: // 2+
		return fmt.Sprintf(`static Obj* %s(Obj** captures, Obj** args, int n) {
    (void)captures;
    if (n < 2) return NULL;
    return %s(args[0], args[1]);
}
`, wrapperName, cFn)
	}
}

// compileLambda compiles a lambda expression into a closure.
func (c *Compiler) compileLambda(expr *ast.Value) (CValue, error) {
	args := expr.Cdr
	if args == nil || ast.IsNil(args) {
		return CValue{}, fmt.Errorf("lambda: missing parameters")
	}

	params := args.Car
	body := args.Cdr.Car

	// Collect parameter names
	var paramNames []string
	for !ast.IsNil(params) && ast.IsCell(params) {
		if ast.IsSym(params.Car) {
			paramNames = append(paramNames, params.Car.Str)
		}
		params = params.Cdr
	}

	// Find free variables (captured)
	freeVars := c.findFreeVars(body, paramNames)

	// Generate unique function name
	fnName := fmt.Sprintf("_lambda_%d", c.tempCounter)
	c.tempCounter++

	// Generate the function definition
	fnDef := c.genLambdaFunc(fnName, paramNames, freeVars, body)
	c.funcDefs = append(c.funcDefs, fnDef)

	// Build capture array
	if len(freeVars) == 0 {
		return CValue{
			Expr:  fmt.Sprintf("mk_closure(%s, NULL, NULL, 0, %d)", fnName, len(paramNames)),
			Owned: true,
		}, nil
	}

	// Generate a statement expression that builds captures and creates closure
	var sb strings.Builder
	sb.WriteString("({\n")
	sb.WriteString(fmt.Sprintf("    Obj* _caps[%d];\n", len(freeVars)))
	sb.WriteString(fmt.Sprintf("    GenRef* _refs[%d];\n", len(freeVars)))
	for i, cap := range freeVars {
		info, _ := c.lookup(cap)
		sb.WriteString(fmt.Sprintf("    _caps[%d] = %s;\n", i, info.CName))
		sb.WriteString(fmt.Sprintf("    _refs[%d] = genref_from_obj(%s, \"%s\");\n", i, info.CName, cap))
	}
	sb.WriteString(fmt.Sprintf("    mk_closure(%s, _caps, _refs, %d, %d);\n", fnName, len(freeVars), len(paramNames)))
	sb.WriteString("})")

	return CValue{
		Expr:  sb.String(),
		Owned: true,
	}, nil
}

func (c *Compiler) findFreeVars(body *ast.Value, bound []string) []string {
	boundSet := make(map[string]bool)
	for _, b := range bound {
		boundSet[b] = true
	}
	freeSet := make(map[string]bool)
	c.collectFreeVars(body, boundSet, freeSet)

	var result []string
	for v := range freeSet {
		result = append(result, v)
	}
	return result
}

func (c *Compiler) collectFreeVars(expr *ast.Value, bound, free map[string]bool) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	switch expr.Tag {
	case ast.TSym:
		if !bound[expr.Str] {
			if _, ok := c.lookup(expr.Str); ok {
				free[expr.Str] = true
			}
		}
	case ast.TCell:
		if ast.IsSym(expr.Car) {
			switch expr.Car.Str {
			case "lambda":
				// Add params to bound in body
				params := expr.Cdr.Car
				newBound := make(map[string]bool)
				for k, v := range bound {
					newBound[k] = v
				}
				for !ast.IsNil(params) && ast.IsCell(params) {
					if ast.IsSym(params.Car) {
						newBound[params.Car.Str] = true
					}
					params = params.Cdr
				}
				c.collectFreeVars(expr.Cdr.Cdr.Car, newBound, free)
				return
			case "let", "letrec":
				bindings := expr.Cdr.Car
				bodyExpr := expr.Cdr.Cdr.Car
				newBound := make(map[string]bool)
				for k, v := range bound {
					newBound[k] = v
				}
				for !ast.IsNil(bindings) && ast.IsCell(bindings) {
					binding := bindings.Car
					if ast.IsCell(binding) && ast.IsSym(binding.Car) {
						newBound[binding.Car.Str] = true
						c.collectFreeVars(binding.Cdr.Car, bound, free)
					}
					bindings = bindings.Cdr
				}
				c.collectFreeVars(bodyExpr, newBound, free)
				return
			case "quote":
				return // Don't descend into quoted expressions
			}
		}
		// Default: recurse on car and cdr
		c.collectFreeVars(expr.Car, bound, free)
		c.collectFreeVars(expr.Cdr, bound, free)
	}
}

func (c *Compiler) genLambdaFunc(fnName string, params, captures []string, body *ast.Value) string {
	var sb strings.Builder
	sb.WriteString(fmt.Sprintf("static Obj* %s(Obj** _captures, Obj** _args, int _n) {\n", fnName))
	sb.WriteString("    (void)_n;\n")

	// Bind captures
	for i, cap := range captures {
		cName := c.cIdent(cap)
		sb.WriteString(fmt.Sprintf("    Obj* %s = _captures[%d];\n", cName, i))
	}

	// Bind parameters
	for i, param := range params {
		cName := c.cIdent(param)
		sb.WriteString(fmt.Sprintf("    Obj* %s = _args[%d];\n", cName, i))
	}

	// Create a scope for this function
	scope := make(map[string]VarInfo)
	for _, cap := range captures {
		scope[cap] = VarInfo{CName: c.cIdent(cap)}
	}
	for _, param := range params {
		scope[param] = VarInfo{CName: c.cIdent(param)}
	}
	c.pushScope(scope)

	// Compile body
	cv, err := c.compileExpr(body)
	c.popScope()

	if err != nil {
		sb.WriteString(fmt.Sprintf("    /* compile error: %v */\n", err))
		sb.WriteString("    return NULL;\n")
	} else {
		// If the return value is borrowed, increment ref so caller receives owned value
		if !cv.Owned {
			sb.WriteString(fmt.Sprintf("    Obj* _ret = %s;\n", cv.Expr))
			sb.WriteString("    if (_ret) inc_ref(_ret);\n")
			sb.WriteString("    return _ret;\n")
		} else {
			sb.WriteString(fmt.Sprintf("    return %s;\n", cv.Expr))
		}
	}

	sb.WriteString("}\n")
	return sb.String()
}

func (c *Compiler) genCaptureArray(captures []string) string {
	var sb strings.Builder
	sb.WriteString("{\n")
	sb.WriteString(fmt.Sprintf("    Obj* _caps[%d];\n", len(captures)))
	sb.WriteString(fmt.Sprintf("    GenRef* _refs[%d];\n", len(captures)))
	for i, cap := range captures {
		info, _ := c.lookup(cap)
		sb.WriteString(fmt.Sprintf("    _caps[%d] = %s;\n", i, info.CName))
		sb.WriteString(fmt.Sprintf("    _refs[%d] = genref_from_obj(%s, \"%s\");\n", i, info.CName, cap))
	}
	sb.WriteString("}\n")
	return sb.String()
}

// compileQuote compiles a quoted expression to a literal value.
func (c *Compiler) compileQuote(expr *ast.Value) (CValue, error) {
	return c.quoteToCExpr(expr)
}

func (c *Compiler) quoteToCExpr(expr *ast.Value) (CValue, error) {
	if expr == nil || ast.IsNil(expr) {
		return CValue{Expr: "NULL", IsNil: true}, nil
	}

	switch expr.Tag {
	case ast.TInt:
		return CValue{Expr: fmt.Sprintf("mk_int(%d)", expr.Int), Owned: true}, nil
	case ast.TFloat:
		return CValue{Expr: fmt.Sprintf("mk_float(%f)", expr.Float), Owned: true}, nil
	case ast.TChar:
		return CValue{Expr: fmt.Sprintf("mk_int(%d)", expr.Int), Owned: true}, nil
	case ast.TSym:
		// Symbols become interned symbols
		return CValue{Expr: fmt.Sprintf("mk_sym(\"%s\")", expr.Str), Owned: true}, nil
	case ast.TCell:
		carVal, err := c.quoteToCExpr(expr.Car)
		if err != nil {
			return CValue{}, err
		}
		cdrVal, err := c.quoteToCExpr(expr.Cdr)
		if err != nil {
			return CValue{}, err
		}
		return CValue{
			Expr:  fmt.Sprintf("mk_pair(%s, %s)", carVal.Expr, cdrVal.Expr),
			Owned: true,
		}, nil
	default:
		return CValue{Expr: "NULL", IsNil: true}, nil
	}
}

// compileAnd compiles (and e1 e2 ...) with short-circuit evaluation.
func (c *Compiler) compileAnd(args *ast.Value) (CValue, error) {
	if ast.IsNil(args) {
		return CValue{Expr: "mk_int(1)", Owned: true}, nil // (and) => true
	}

	var exprs []*ast.Value
	for !ast.IsNil(args) && ast.IsCell(args) {
		exprs = append(exprs, args.Car)
		args = args.Cdr
	}

	if len(exprs) == 1 {
		return c.compileExpr(exprs[0])
	}

	resName := c.newTemp()
	var sb strings.Builder
	sb.WriteString("({\n")
	sb.WriteString(fmt.Sprintf("    Obj* %s = NULL;\n", resName))

	for i, expr := range exprs {
		cv, err := c.compileExpr(expr)
		if err != nil {
			return CValue{}, err
		}
		tmp := c.newTemp()
		sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", tmp, cv.Expr))

		if i < len(exprs)-1 {
			sb.WriteString(fmt.Sprintf("    if (!is_truthy(%s)) {\n", tmp))
			sb.WriteString(fmt.Sprintf("        %s = %s;\n", resName, tmp))
			sb.WriteString("    } else {\n")
			if cv.Owned {
				sb.WriteString(fmt.Sprintf("        dec_ref(%s);\n", tmp))
			}
		} else {
			sb.WriteString(fmt.Sprintf("    %s = %s;\n", resName, tmp))
		}
	}

	// Close all the else braces
	for i := 0; i < len(exprs)-1; i++ {
		sb.WriteString("    }\n")
	}

	sb.WriteString(fmt.Sprintf("    %s;\n", resName))
	sb.WriteString("})")

	return CValue{Expr: sb.String(), Owned: true}, nil
}

// compileOr compiles (or e1 e2 ...) with short-circuit evaluation.
func (c *Compiler) compileOr(args *ast.Value) (CValue, error) {
	if ast.IsNil(args) {
		return CValue{Expr: "NULL", IsNil: true}, nil // (or) => false/nil
	}

	var exprs []*ast.Value
	for !ast.IsNil(args) && ast.IsCell(args) {
		exprs = append(exprs, args.Car)
		args = args.Cdr
	}

	if len(exprs) == 1 {
		return c.compileExpr(exprs[0])
	}

	resName := c.newTemp()
	var sb strings.Builder
	sb.WriteString("({\n")
	sb.WriteString(fmt.Sprintf("    Obj* %s = NULL;\n", resName))

	for i, expr := range exprs {
		cv, err := c.compileExpr(expr)
		if err != nil {
			return CValue{}, err
		}
		tmp := c.newTemp()
		sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", tmp, cv.Expr))

		if i < len(exprs)-1 {
			sb.WriteString(fmt.Sprintf("    if (is_truthy(%s)) {\n", tmp))
			sb.WriteString(fmt.Sprintf("        %s = %s;\n", resName, tmp))
			sb.WriteString("    } else {\n")
			if cv.Owned {
				sb.WriteString(fmt.Sprintf("        dec_ref(%s);\n", tmp))
			}
		} else {
			sb.WriteString(fmt.Sprintf("    %s = %s;\n", resName, tmp))
		}
	}

	// Close all the else braces
	for i := 0; i < len(exprs)-1; i++ {
		sb.WriteString("    }\n")
	}

	sb.WriteString(fmt.Sprintf("    %s;\n", resName))
	sb.WriteString("})")

	return CValue{Expr: sb.String(), Owned: true}, nil
}

// compileTry compiles (try expr handler) using exception runtime.
func (c *Compiler) compileTry(args *ast.Value) (CValue, error) {
	if ast.IsNil(args) || ast.IsNil(args.Cdr) {
		return CValue{}, fmt.Errorf("try: requires expression and handler")
	}

	tryExpr := args.Car
	handlerExpr := args.Cdr.Car

	resName := c.newTemp()
	errName := c.newTemp()

	var sb strings.Builder
	sb.WriteString("({\n")
	sb.WriteString(fmt.Sprintf("    Obj* %s = NULL;\n", resName))
	sb.WriteString(fmt.Sprintf("    Obj* %s = NULL;\n", errName))
	sb.WriteString("    TRY_BEGIN {\n")

	tryVal, err := c.compileExpr(tryExpr)
	if err != nil {
		return CValue{}, err
	}
	sb.WriteString(fmt.Sprintf("        %s = %s;\n", resName, tryVal.Expr))

	sb.WriteString(fmt.Sprintf("    } TRY_CATCH(%s) {\n", errName))

	// Compile handler with error bound
	handlerScope := make(map[string]VarInfo)
	handlerScope["error"] = VarInfo{CName: errName}
	c.pushScope(handlerScope)

	handlerVal, err := c.compileExpr(handlerExpr)
	c.popScope()

	if err != nil {
		return CValue{}, err
	}
	sb.WriteString(fmt.Sprintf("        %s = %s;\n", resName, handlerVal.Expr))

	sb.WriteString("    } TRY_END;\n")
	sb.WriteString(fmt.Sprintf("    %s;\n", resName))
	sb.WriteString("})")

	return CValue{Expr: sb.String(), Owned: true}, nil
}

// compileError compiles (error msg) to throw an exception.
func (c *Compiler) compileError(args *ast.Value) (CValue, error) {
	if ast.IsNil(args) {
		return CValue{Expr: "THROW(mk_error(\"error\"))", Owned: true}, nil
	}

	msgExpr := args.Car
	msgVal, err := c.compileExpr(msgExpr)
	if err != nil {
		return CValue{}, err
	}

	return CValue{
		Expr:  fmt.Sprintf("THROW(mk_error_obj(%s))", msgVal.Expr),
		Owned: true,
	}, nil
}
