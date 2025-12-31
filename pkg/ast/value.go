package ast

import (
	"fmt"
	"strconv"
	"strings"
)

// Tag represents the type of a Value
type Tag int

const (
	TInt Tag = iota
	TSym
	TCell
	TNil
	TPrim
	TMenv
	TCode
	TLambda
	TRecLambda // Recursive lambda with self-reference
	TError     // Error value
	TChar      // Character value
	TFloat     // Floating point value (float64)
)

// PrimFn is a primitive function signature
type PrimFn func(args *Value, menv *Value) *Value

// HandlerFn is a handler function for meta-environments
type HandlerFn func(exp *Value, menv *Value) *Value

// Value is the core tagged union type for all values
type Value struct {
	Tag Tag

	// TInt, TChar
	Int int64

	// TFloat
	Float float64

	// TSym, TCode
	Str string

	// TCell
	Car *Value
	Cdr *Value

	// TPrim
	Prim PrimFn

	// TMenv
	Env     *Value
	Parent  *Value
	HApp    HandlerFn
	HLet    HandlerFn
	HIf     HandlerFn
	HLit    HandlerFn
	HVar    HandlerFn

	// TLambda, TRecLambda
	Params   *Value
	Body     *Value
	LamEnv   *Value
	SelfName *Value // For TRecLambda only
}

// Nil is the singleton nil value
var Nil = &Value{Tag: TNil}

// NewInt creates an integer value
func NewInt(i int64) *Value {
	return &Value{Tag: TInt, Int: i}
}

// NewSym creates a symbol value
func NewSym(s string) *Value {
	return &Value{Tag: TSym, Str: s}
}

// NewCell creates a cons cell
func NewCell(car, cdr *Value) *Value {
	return &Value{Tag: TCell, Car: car, Cdr: cdr}
}

// NewPrim creates a primitive function value
func NewPrim(fn PrimFn) *Value {
	return &Value{Tag: TPrim, Prim: fn}
}

// NewCode creates a code (generated C) value
func NewCode(s string) *Value {
	return &Value{Tag: TCode, Str: s}
}

// NewLambda creates a lambda/closure value
func NewLambda(params, body, env *Value) *Value {
	return &Value{
		Tag:    TLambda,
		Params: params,
		Body:   body,
		LamEnv: env,
	}
}

// NewRecLambda creates a recursive lambda with self-reference
func NewRecLambda(selfName, params, body, env *Value) *Value {
	return &Value{
		Tag:      TRecLambda,
		SelfName: selfName,
		Params:   params,
		Body:     body,
		LamEnv:   env,
	}
}

// NewError creates an error value
func NewError(msg string) *Value {
	return &Value{Tag: TError, Str: msg}
}

// NewChar creates a character value
func NewChar(c rune) *Value {
	return &Value{Tag: TChar, Int: int64(c)}
}

// NewFloat creates a floating point value
func NewFloat(f float64) *Value {
	return &Value{Tag: TFloat, Float: f}
}

// NewMenv creates a meta-environment value
func NewMenv(env, parent *Value, hApp, hLet, hIf, hLit, hVar HandlerFn) *Value {
	return &Value{
		Tag:    TMenv,
		Env:    env,
		Parent: parent,
		HApp:   hApp,
		HLet:   hLet,
		HIf:    hIf,
		HLit:   hLit,
		HVar:   hVar,
	}
}

// IsNil checks if a value is nil
func IsNil(v *Value) bool {
	return v == nil || v.Tag == TNil
}

// IsCode checks if a value is generated code
func IsCode(v *Value) bool {
	return v != nil && v.Tag == TCode
}

// IsSym checks if a value is a symbol
func IsSym(v *Value) bool {
	return v != nil && v.Tag == TSym
}

// IsInt checks if a value is an integer
func IsInt(v *Value) bool {
	return v != nil && v.Tag == TInt
}

// IsCell checks if a value is a cons cell
func IsCell(v *Value) bool {
	return v != nil && v.Tag == TCell
}

// IsLambda checks if a value is a lambda
func IsLambda(v *Value) bool {
	return v != nil && v.Tag == TLambda
}

// IsRecLambda checks if a value is a recursive lambda
func IsRecLambda(v *Value) bool {
	return v != nil && v.Tag == TRecLambda
}

// IsError checks if a value is an error
func IsError(v *Value) bool {
	return v != nil && v.Tag == TError
}

// IsChar checks if a value is a character
func IsChar(v *Value) bool {
	return v != nil && v.Tag == TChar
}

// IsFloat checks if a value is a floating point number
func IsFloat(v *Value) bool {
	return v != nil && v.Tag == TFloat
}

// IsPrim checks if a value is a primitive
func IsPrim(v *Value) bool {
	return v != nil && v.Tag == TPrim
}

// IsMenv checks if a value is a meta-environment
func IsMenv(v *Value) bool {
	return v != nil && v.Tag == TMenv
}

// SymEq compares two symbols
func SymEq(s1, s2 *Value) bool {
	if s1 == nil || s2 == nil {
		return false
	}
	if s1.Tag != TSym || s2.Tag != TSym {
		return false
	}
	return s1.Str == s2.Str
}

// SymEqStr compares a symbol to a string
func SymEqStr(s *Value, str string) bool {
	if s == nil || s.Tag != TSym {
		return false
	}
	return s.Str == str
}

// List helpers
func List1(a *Value) *Value {
	return NewCell(a, Nil)
}

func List2(a, b *Value) *Value {
	return NewCell(a, NewCell(b, Nil))
}

func List3(a, b, c *Value) *Value {
	return NewCell(a, NewCell(b, NewCell(c, Nil)))
}

// ListLen returns the length of a list
func ListLen(v *Value) int {
	n := 0
	for !IsNil(v) && IsCell(v) {
		n++
		v = v.Cdr
	}
	return n
}

// ListToSlice converts a list to a slice
func ListToSlice(v *Value) []*Value {
	var result []*Value
	for !IsNil(v) && IsCell(v) {
		result = append(result, v.Car)
		v = v.Cdr
	}
	return result
}

// SliceToList converts a slice to a list
func SliceToList(items []*Value) *Value {
	result := Nil
	for i := len(items) - 1; i >= 0; i-- {
		result = NewCell(items[i], result)
	}
	return result
}

// String returns a string representation of a value
func (v *Value) String() string {
	if v == nil {
		return "nil"
	}
	switch v.Tag {
	case TInt:
		return strconv.FormatInt(v.Int, 10)
	case TSym:
		return v.Str
	case TCode:
		return v.Str
	case TCell:
		return listToString(v)
	case TNil:
		return "()"
	case TPrim:
		return "#<prim>"
	case TLambda:
		return "#<lambda>"
	case TRecLambda:
		return "#<rec-lambda>"
	case TError:
		return fmt.Sprintf("#<error: %s>", v.Str)
	case TChar:
		return charToString(rune(v.Int))
	case TFloat:
		return strconv.FormatFloat(v.Float, 'g', -1, 64)
	case TMenv:
		return "#<menv>"
	default:
		return "?"
	}
}

func listToString(v *Value) string {
	var sb strings.Builder
	sb.WriteByte('(')
	first := true
	for !IsNil(v) && IsCell(v) {
		if !first {
			sb.WriteByte(' ')
		}
		first = false
		sb.WriteString(v.Car.String())
		v = v.Cdr
	}
	if !IsNil(v) {
		// Improper list
		sb.WriteString(" . ")
		sb.WriteString(v.String())
	}
	sb.WriteByte(')')
	return sb.String()
}

func charToString(c rune) string {
	switch c {
	case '\n':
		return "#\\newline"
	case '\t':
		return "#\\tab"
	case '\r':
		return "#\\return"
	case ' ':
		return "#\\space"
	default:
		return fmt.Sprintf("#\\%c", c)
	}
}

// TagName returns the name of a tag
func TagName(t Tag) string {
	switch t {
	case TInt:
		return "INT"
	case TSym:
		return "SYM"
	case TCell:
		return "CELL"
	case TNil:
		return "NIL"
	case TPrim:
		return "PRIM"
	case TMenv:
		return "MENV"
	case TCode:
		return "CODE"
	case TLambda:
		return "LAMBDA"
	case TRecLambda:
		return "RECLAMBDA"
	case TError:
		return "ERROR"
	case TChar:
		return "CHAR"
	case TFloat:
		return "FLOAT"
	default:
		return fmt.Sprintf("UNKNOWN(%d)", t)
	}
}
