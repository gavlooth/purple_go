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
	TBox       // Mutable reference cell (for set!)
	TCont      // First-class continuation
	TChan      // CSP channel (Go channel based, for OS threads)
	TGreenChan // Green channel (continuation based, for green threads)
	TAtom      // Atomic reference (for shared state)
	TThread    // OS thread handle
	TProcess   // Green thread / process
	TUserType  // User-defined type instance
)

// PrimFn is a primitive function signature
type PrimFn func(args *Value, menv *Value) *Value

// HandlerFn is a handler function for meta-environments
type HandlerFn func(exp *Value, menv *Value) *Value

// Handler indices for the 9-handler table
const (
	HIdxLit  = 0 // literal handler
	HIdxVar  = 1 // variable lookup handler
	HIdxLam  = 2 // lambda handler
	HIdxApp  = 3 // application handler
	HIdxIf   = 4 // conditional handler
	HIdxLft  = 5 // lift handler
	HIdxRun  = 6 // run handler
	HIdxEM   = 7 // EM (escape to meta) handler
	HIdxClam = 8 // clambda handler
)

// Handler names for get-meta/set-meta!
var HandlerNames = map[string]int{
	"lit":  HIdxLit,
	"var":  HIdxVar,
	"lam":  HIdxLam,
	"app":  HIdxApp,
	"if":   HIdxIf,
	"lft":  HIdxLft,
	"run":  HIdxRun,
	"em":   HIdxEM,
	"clam": HIdxClam,
}

// HandlerWrapper wraps a handler - either native (HandlerFn) or closure
type HandlerWrapper struct {
	Native  HandlerFn // For built-in handlers
	Closure *Value    // For user-defined handlers (lambda)
}

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

	// TMenv - restructured for tower of interpreters
	Env      *Value             // Variable bindings
	Parent   *Value             // Parent meta-environment (lazy)
	Level    int                // Tower level (0 = base)
	Handlers [9]*HandlerWrapper // 9-handler table

	// TLambda, TRecLambda
	Params   *Value
	Body     *Value
	LamEnv   *Value
	SelfName *Value // For TRecLambda only

	// TBox - mutable reference cell
	BoxValue *Value

	// TCont - continuation
	ContFn   func(*Value) *Value // Continuation function
	ContMenv *Value              // Captured meta-environment

	// TChan - channel (Go channel based, for OS threads)
	ChanSend chan *Value // For sending
	ChanRecv chan *Value // For receiving (same as Send for normal channels)
	ChanCap  int         // Capacity (0 = unbuffered)

	// TGreenChan - green channel (continuation based)
	GreenChan interface{} // *eval.GreenChannel (use interface to avoid import cycle)

	// TAtom - atomic reference
	AtomValue *Value // Current value (use sync/atomic for actual atomicity in Go)

	// TThread - OS thread handle
	ThreadDone   chan *Value // Channel to receive result
	ThreadResult *Value      // Result when done

	// TProcess - green thread
	ProcCont   *Value // Current continuation
	ProcState  int    // 0=ready, 1=running, 2=parked, 3=done
	ProcResult *Value // Result when done

	// TUserType - user-defined type instance
	UserTypeName       string            // Type name (e.g., "Node")
	UserTypeFields     map[string]*Value // Field name -> value
	UserTypeFieldOrder []string          // Field names in definition order
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

// NewBox creates a mutable reference cell
func NewBox(v *Value) *Value {
	return &Value{Tag: TBox, BoxValue: v}
}

// NewCont creates a continuation value
func NewCont(fn func(*Value) *Value, menv *Value) *Value {
	return &Value{Tag: TCont, ContFn: fn, ContMenv: menv}
}

// NewChan creates a channel value (Go channel based, for OS threads)
func NewChan(capacity int) *Value {
	ch := make(chan *Value, capacity)
	return &Value{
		Tag:      TChan,
		ChanSend: ch,
		ChanRecv: ch,
		ChanCap:  capacity,
	}
}

// NewGreenChan creates a green channel value (continuation based)
func NewGreenChan(greenChan interface{}) *Value {
	return &Value{
		Tag:       TGreenChan,
		GreenChan: greenChan,
	}
}

// NewAtom creates an atomic reference
func NewAtom(val *Value) *Value {
	return &Value{
		Tag:       TAtom,
		AtomValue: val,
	}
}

// NewThread creates an OS thread handle
func NewThread() *Value {
	return &Value{
		Tag:        TThread,
		ThreadDone: make(chan *Value, 1),
	}
}

// NewProcess creates a green thread/process value
func NewProcess(cont *Value) *Value {
	return &Value{
		Tag:       TProcess,
		ProcCont:  cont,
		ProcState: 0, // ready
	}
}

// NewUserType creates a user-defined type instance
// fieldOrder specifies the order of fields for index-based access
func NewUserType(typeName string, fields map[string]*Value, fieldOrder []string) *Value {
	return &Value{
		Tag:                TUserType,
		UserTypeName:       typeName,
		UserTypeFields:     fields,
		UserTypeFieldOrder: fieldOrder,
	}
}

// IsUserType checks if value is a user-defined type
func IsUserType(v *Value) bool {
	return v != nil && v.Tag == TUserType
}

// IsUserTypeOf checks if value is an instance of specific user type
func IsUserTypeOf(v *Value, typeName string) bool {
	return v != nil && v.Tag == TUserType && v.UserTypeName == typeName
}

// UserTypeGetField gets a field value from a user-defined type
func UserTypeGetField(v *Value, fieldName string) *Value {
	if v == nil || v.Tag != TUserType || v.UserTypeFields == nil {
		return nil
	}
	return v.UserTypeFields[fieldName]
}

// UserTypeSetField sets a field value in a user-defined type
func UserTypeSetField(v *Value, fieldName string, val *Value) {
	if v != nil && v.Tag == TUserType && v.UserTypeFields != nil {
		v.UserTypeFields[fieldName] = val
	}
}

// NewMenv creates a meta-environment value with handler table
func NewMenv(env, parent *Value, level int, handlers [9]*HandlerWrapper) *Value {
	return &Value{
		Tag:      TMenv,
		Env:      env,
		Parent:   parent,
		Level:    level,
		Handlers: handlers,
	}
}

// NewMenvWithDefaults creates a menv with default handlers (set later by eval)
func NewMenvWithDefaults(env, parent *Value, level int) *Value {
	return &Value{
		Tag:    TMenv,
		Env:    env,
		Parent: parent,
		Level:  level,
		// Handlers will be set by evaluator
	}
}

// WrapNativeHandler wraps a native handler function
func WrapNativeHandler(fn HandlerFn) *HandlerWrapper {
	return &HandlerWrapper{Native: fn}
}

// WrapClosureHandler wraps a closure as a handler
func WrapClosureHandler(closure *Value) *HandlerWrapper {
	return &HandlerWrapper{Closure: closure}
}

// GetHandler returns handler at index
func (v *Value) GetHandler(idx int) *HandlerWrapper {
	if v == nil || v.Tag != TMenv || idx < 0 || idx > 8 {
		return nil
	}
	return v.Handlers[idx]
}

// SetHandler returns a new menv with updated handler at index
func (v *Value) SetHandler(idx int, handler *HandlerWrapper) *Value {
	if v == nil || v.Tag != TMenv || idx < 0 || idx > 8 {
		return v
	}
	// Copy handlers array
	var newHandlers [9]*HandlerWrapper
	copy(newHandlers[:], v.Handlers[:])
	newHandlers[idx] = handler
	return NewMenv(v.Env, v.Parent, v.Level, newHandlers)
}

// CopyHandlers returns a copy of the handler table
func (v *Value) CopyHandlers() [9]*HandlerWrapper {
	var result [9]*HandlerWrapper
	if v != nil && v.Tag == TMenv {
		copy(result[:], v.Handlers[:])
	}
	return result
}

// WithEnv returns a new menv with updated environment
func (v *Value) WithEnv(newEnv *Value) *Value {
	if v == nil || v.Tag != TMenv {
		return v
	}
	return NewMenv(newEnv, v.Parent, v.Level, v.Handlers)
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

// IsBox checks if a value is a mutable box
func IsBox(v *Value) bool {
	return v != nil && v.Tag == TBox
}

// IsCont checks if a value is a continuation
func IsCont(v *Value) bool {
	return v != nil && v.Tag == TCont
}

// IsChan checks if a value is a channel (Go channel based)
func IsChan(v *Value) bool {
	return v != nil && v.Tag == TChan
}

// IsGreenChan checks if a value is a green channel
func IsGreenChan(v *Value) bool {
	return v != nil && v.Tag == TGreenChan
}

// IsAtom checks if a value is an atomic reference
func IsAtom(v *Value) bool {
	return v != nil && v.Tag == TAtom
}

// IsThread checks if a value is an OS thread handle
func IsThread(v *Value) bool {
	return v != nil && v.Tag == TThread
}

// IsProcess checks if a value is a process/green thread
func IsProcess(v *Value) bool {
	return v != nil && v.Tag == TProcess
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
	case TBox:
		return fmt.Sprintf("#<box %s>", v.BoxValue.String())
	case TCont:
		return "#<continuation>"
	case TChan:
		return fmt.Sprintf("#<channel cap=%d>", v.ChanCap)
	case TGreenChan:
		return "#<green-channel>"
	case TAtom:
		return fmt.Sprintf("#<atom %s>", v.AtomValue.String())
	case TThread:
		return "#<thread>"
	case TProcess:
		states := []string{"ready", "running", "parked", "done"}
		state := "unknown"
		if v.ProcState >= 0 && v.ProcState < len(states) {
			state = states[v.ProcState]
		}
		return fmt.Sprintf("#<process %s>", state)
	case TUserType:
		var sb strings.Builder
		sb.WriteString("#<")
		sb.WriteString(v.UserTypeName)
		for _, fieldName := range v.UserTypeFieldOrder {
			sb.WriteString(" ")
			sb.WriteString(fieldName)
			sb.WriteString("=")
			if val, ok := v.UserTypeFields[fieldName]; ok {
				sb.WriteString(val.String())
			} else {
				sb.WriteString("nil")
			}
		}
		sb.WriteString(">")
		return sb.String()
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
	case TBox:
		return "BOX"
	case TCont:
		return "CONT"
	case TChan:
		return "CHAN"
	case TGreenChan:
		return "GREEN_CHAN"
	case TAtom:
		return "ATOM"
	case TThread:
		return "THREAD"
	case TProcess:
		return "PROCESS"
	case TUserType:
		return "USERTYPE"
	default:
		return fmt.Sprintf("UNKNOWN(%d)", t)
	}
}
