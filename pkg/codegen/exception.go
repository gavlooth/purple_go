package codegen

import (
	"fmt"
	"io"
	"strings"

	"purple_go/pkg/ast"
)

// CleanupPoint represents a point in code where cleanup may be needed
// during exception unwinding
type CleanupPoint struct {
	ID           int      // Unique identifier
	LiveVars     []string // Variables that need cleanup at this point
	VarTypes     []string // Types of each variable (for type-specific release)
	SourceLine   int      // Source line for debugging
	InTryBlock   bool     // Whether this point is inside a try block
	TryBlockID   int      // Which try block this belongs to
}

// LandingPad represents exception handling code for a try block
type LandingPad struct {
	TryBlockID    int             // Unique identifier for the try block
	CleanupPoints []*CleanupPoint // Points that need cleanup if exception occurs
	HandlerLabel  string          // Label for the handler code
	CleanupLabel  string          // Label for cleanup code
	ResumeLabel   string          // Label to resume after cleanup
	CatchVar      string          // Variable to bind caught exception
}

// ExceptionContext tracks exception handling during code generation
type ExceptionContext struct {
	CurrentTryBlock int              // Current try block nesting level
	TryBlocks       []*LandingPad    // Stack of active try blocks
	AllCleanupPoints []*CleanupPoint // All cleanup points in the function
	NextCleanupID   int              // Counter for cleanup point IDs
	NextTryBlockID  int              // Counter for try block IDs
}

// NewExceptionContext creates a new exception tracking context
func NewExceptionContext() *ExceptionContext {
	return &ExceptionContext{
		CurrentTryBlock: -1,
		TryBlocks:       make([]*LandingPad, 0),
		AllCleanupPoints: make([]*CleanupPoint, 0),
	}
}

// EnterTryBlock marks entry into a try block
func (ctx *ExceptionContext) EnterTryBlock(catchVar string) *LandingPad {
	pad := &LandingPad{
		TryBlockID:    ctx.NextTryBlockID,
		CleanupPoints: make([]*CleanupPoint, 0),
		HandlerLabel:  fmt.Sprintf("_handler_%d", ctx.NextTryBlockID),
		CleanupLabel:  fmt.Sprintf("_cleanup_%d", ctx.NextTryBlockID),
		ResumeLabel:   fmt.Sprintf("_resume_%d", ctx.NextTryBlockID),
		CatchVar:      catchVar,
	}
	ctx.NextTryBlockID++
	ctx.TryBlocks = append(ctx.TryBlocks, pad)
	ctx.CurrentTryBlock = len(ctx.TryBlocks) - 1
	return pad
}

// ExitTryBlock marks exit from a try block
func (ctx *ExceptionContext) ExitTryBlock() *LandingPad {
	if ctx.CurrentTryBlock < 0 || len(ctx.TryBlocks) == 0 {
		return nil
	}
	pad := ctx.TryBlocks[ctx.CurrentTryBlock]
	ctx.TryBlocks = ctx.TryBlocks[:len(ctx.TryBlocks)-1]
	ctx.CurrentTryBlock = len(ctx.TryBlocks) - 1
	return pad
}

// AddCleanupPoint adds a cleanup point for a variable allocation
func (ctx *ExceptionContext) AddCleanupPoint(varName, varType string, sourceLine int) *CleanupPoint {
	cp := &CleanupPoint{
		ID:         ctx.NextCleanupID,
		LiveVars:   []string{varName},
		VarTypes:   []string{varType},
		SourceLine: sourceLine,
		InTryBlock: ctx.CurrentTryBlock >= 0,
	}
	if ctx.CurrentTryBlock >= 0 {
		cp.TryBlockID = ctx.TryBlocks[ctx.CurrentTryBlock].TryBlockID
		ctx.TryBlocks[ctx.CurrentTryBlock].CleanupPoints = append(
			ctx.TryBlocks[ctx.CurrentTryBlock].CleanupPoints, cp)
	}
	ctx.NextCleanupID++
	ctx.AllCleanupPoints = append(ctx.AllCleanupPoints, cp)
	return cp
}

// ExtendCleanupPoint adds another variable to an existing cleanup point
func (ctx *ExceptionContext) ExtendCleanupPoint(cp *CleanupPoint, varName, varType string) {
	cp.LiveVars = append(cp.LiveVars, varName)
	cp.VarTypes = append(cp.VarTypes, varType)
}

// GetCurrentCleanupVars returns all variables that need cleanup at the current point
func (ctx *ExceptionContext) GetCurrentCleanupVars() []string {
	var result []string
	for _, cp := range ctx.AllCleanupPoints {
		result = append(result, cp.LiveVars...)
	}
	return result
}

// ExceptionCodeGenerator generates C code for exception handling
type ExceptionCodeGenerator struct {
	w        io.Writer
	ctx      *ExceptionContext
	registry *TypeRegistry
}

// NewExceptionCodeGenerator creates a new exception code generator
func NewExceptionCodeGenerator(w io.Writer, registry *TypeRegistry) *ExceptionCodeGenerator {
	return &ExceptionCodeGenerator{
		w:        w,
		ctx:      NewExceptionContext(),
		registry: registry,
	}
}

func (g *ExceptionCodeGenerator) emit(format string, args ...interface{}) {
	fmt.Fprintf(g.w, format, args...)
}

// GenerateExceptionRuntime generates the C runtime support for exceptions
func (g *ExceptionCodeGenerator) GenerateExceptionRuntime() {
	g.emit(`/* ========== Exception Handling Runtime ========== */
/* ASAP-compatible exception handling with deterministic cleanup */
/* Uses setjmp/longjmp for non-local control flow */

#include <setjmp.h>

/* Exception context for stack unwinding */
typedef struct ExceptionContext ExceptionContext;
struct ExceptionContext {
    jmp_buf jump_buffer;
    Obj* exception_value;
    ExceptionContext* parent;
    void** cleanup_stack;
    int cleanup_count;
    int cleanup_capacity;
};

/* Thread-local exception context stack */
static __thread ExceptionContext* g_exception_ctx = NULL;

/* Push a new exception context (entering try block) */
static ExceptionContext* exception_push(void) {
    ExceptionContext* ctx = malloc(sizeof(ExceptionContext));
    if (!ctx) return NULL;
    ctx->exception_value = NULL;
    ctx->parent = g_exception_ctx;
    ctx->cleanup_stack = NULL;
    ctx->cleanup_count = 0;
    ctx->cleanup_capacity = 0;
    g_exception_ctx = ctx;
    return ctx;
}

/* Pop exception context (exiting try block normally) */
static void exception_pop(void) {
    if (!g_exception_ctx) return;
    ExceptionContext* ctx = g_exception_ctx;
    g_exception_ctx = ctx->parent;
    free(ctx->cleanup_stack);
    free(ctx);
}

/* Register a value for cleanup during unwinding */
static void exception_register_cleanup(void* ptr) {
    if (!g_exception_ctx) return;
    ExceptionContext* ctx = g_exception_ctx;
    if (ctx->cleanup_count >= ctx->cleanup_capacity) {
        int new_cap = ctx->cleanup_capacity == 0 ? 8 : ctx->cleanup_capacity * 2;
        void** new_stack = realloc(ctx->cleanup_stack, new_cap * sizeof(void*));
        if (!new_stack) return;
        ctx->cleanup_stack = new_stack;
        ctx->cleanup_capacity = new_cap;
    }
    ctx->cleanup_stack[ctx->cleanup_count++] = ptr;
}

/* Unregister a value (it was freed normally) */
static void exception_unregister_cleanup(void* ptr) {
    if (!g_exception_ctx) return;
    ExceptionContext* ctx = g_exception_ctx;
    for (int i = ctx->cleanup_count - 1; i >= 0; i--) {
        if (ctx->cleanup_stack[i] == ptr) {
            /* Remove by shifting */
            for (int j = i; j < ctx->cleanup_count - 1; j++) {
                ctx->cleanup_stack[j] = ctx->cleanup_stack[j + 1];
            }
            ctx->cleanup_count--;
            return;
        }
    }
}

/* Perform cleanup during unwinding */
static void exception_cleanup(ExceptionContext* ctx) {
    if (!ctx) return;
    /* Free in reverse order (LIFO) */
    for (int i = ctx->cleanup_count - 1; i >= 0; i--) {
        if (ctx->cleanup_stack[i]) {
            dec_ref((Obj*)ctx->cleanup_stack[i]);
        }
    }
    ctx->cleanup_count = 0;
}

/* Throw an exception */
static void exception_throw(Obj* value) {
    if (!g_exception_ctx) {
        /* No handler - print and abort */
        fprintf(stderr, "Uncaught exception: ");
        if (value && value->tag == TAG_ERROR && value->ptr) {
            fprintf(stderr, "%%s\n", (char*)value->ptr);
        } else {
            fprintf(stderr, "<unknown>\n");
        }
        abort();
    }

    ExceptionContext* ctx = g_exception_ctx;
    ctx->exception_value = value;
    if (value) inc_ref(value);

    /* Cleanup current context */
    exception_cleanup(ctx);

    /* Jump to handler */
    longjmp(ctx->jump_buffer, 1);
}

/* Get the current exception value */
static Obj* exception_get_value(void) {
    if (!g_exception_ctx) return NULL;
    return g_exception_ctx->exception_value;
}

/* Macros for try/catch */
#define TRY_BEGIN() do { \
    ExceptionContext* _exc_ctx = exception_push(); \
    if (_exc_ctx && setjmp(_exc_ctx->jump_buffer) == 0) {

#define TRY_CATCH(var) \
    exception_pop(); \
    } else { \
    Obj* var = exception_get_value(); \
    exception_pop();

#define TRY_END() \
    } \
} while(0)

/* Register allocation for cleanup */
#define REGISTER_CLEANUP(ptr) exception_register_cleanup((void*)(ptr))

/* Unregister after normal free */
#define UNREGISTER_CLEANUP(ptr) exception_unregister_cleanup((void*)(ptr))

/* Throw macro */
#define THROW(value) exception_throw((Obj*)(value))

`)
}

// GenerateTryBlock generates code for a try/catch expression
func (g *ExceptionCodeGenerator) GenerateTryBlock(tryExpr, handlerExpr *ast.Value) string {
	pad := g.ctx.EnterTryBlock("_exc")
	defer g.ctx.ExitTryBlock()

	var sb strings.Builder
	sb.WriteString("({\n")
	sb.WriteString("    Obj* _try_result = NULL;\n")
	sb.WriteString("    TRY_BEGIN()\n")
	sb.WriteString(fmt.Sprintf("        _try_result = %s;\n", exprToC(tryExpr)))
	sb.WriteString(fmt.Sprintf("    TRY_CATCH(%s)\n", pad.CatchVar))
	sb.WriteString(fmt.Sprintf("        _try_result = %s;\n", exprToC(handlerExpr)))
	sb.WriteString("    TRY_END();\n")
	sb.WriteString("    _try_result;\n")
	sb.WriteString("})")

	return sb.String()
}

// GenerateCleanupCode generates cleanup code for a landing pad
func (g *ExceptionCodeGenerator) GenerateCleanupCode(pad *LandingPad) string {
	var sb strings.Builder

	sb.WriteString(fmt.Sprintf("%s:\n", pad.CleanupLabel))

	// Free in reverse order
	for i := len(pad.CleanupPoints) - 1; i >= 0; i-- {
		cp := pad.CleanupPoints[i]
		for j := len(cp.LiveVars) - 1; j >= 0; j-- {
			varName := cp.LiveVars[j]
			varType := cp.VarTypes[j]

			// Use type-specific release if available
			if g.registry != nil && g.registry.FindType(varType) != nil {
				sb.WriteString(fmt.Sprintf("    if (%s) release_%s(%s);\n",
					varName, varType, varName))
			} else {
				sb.WriteString(fmt.Sprintf("    if (%s) dec_ref((Obj*)%s);\n",
					varName, varName))
			}
		}
	}

	sb.WriteString(fmt.Sprintf("    goto %s;\n", pad.ResumeLabel))

	return sb.String()
}

// exprToC is a placeholder for expression to C conversion
func exprToC(expr *ast.Value) string {
	if expr == nil {
		return "NULL"
	}
	if ast.IsCode(expr) {
		return expr.Str
	}
	if ast.IsInt(expr) {
		return fmt.Sprintf("mk_int(%d)", expr.Int)
	}
	return "NULL"
}

// AnalyzeExceptionPoints analyzes an expression for potential throw points
func AnalyzeExceptionPoints(expr *ast.Value) []*CleanupPoint {
	ctx := NewExceptionContext()
	analyzeExpr(expr, ctx, 0)
	return ctx.AllCleanupPoints
}

func analyzeExpr(expr *ast.Value, ctx *ExceptionContext, depth int) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	switch expr.Tag {
	case ast.TCell:
		// Check for special forms
		if ast.IsSym(expr.Car) {
			switch expr.Car.Str {
			case "let":
				// Each let binding is a potential cleanup point
				bindings := expr.Cdr.Car
				for !ast.IsNil(bindings) && ast.IsCell(bindings) {
					binding := bindings.Car
					if ast.IsCell(binding) {
						varName := binding.Car.Str
						ctx.AddCleanupPoint(varName, "Obj", depth)
					}
					bindings = bindings.Cdr
				}
				// Analyze body
				if !ast.IsNil(expr.Cdr.Cdr) {
					analyzeExpr(expr.Cdr.Cdr.Car, ctx, depth)
				}

			case "try":
				// Enter try block
				ctx.EnterTryBlock("_exc")
				if !ast.IsNil(expr.Cdr) {
					analyzeExpr(expr.Cdr.Car, ctx, depth+1)
				}
				ctx.ExitTryBlock()

			default:
				// Analyze all subexpressions
				for e := expr.Cdr; !ast.IsNil(e) && ast.IsCell(e); e = e.Cdr {
					analyzeExpr(e.Car, ctx, depth)
				}
			}
		} else {
			// Application - analyze all parts
			analyzeExpr(expr.Car, ctx, depth)
			for e := expr.Cdr; !ast.IsNil(e) && ast.IsCell(e); e = e.Cdr {
				analyzeExpr(e.Car, ctx, depth)
			}
		}
	}
}
