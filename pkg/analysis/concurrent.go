package analysis

import (
	"fmt"
	"purple_go/pkg/ast"
)

// ThreadLocality classifies how data is shared between threads
type ThreadLocality int

const (
	LocalityThreadLocal ThreadLocality = iota // Only accessed by one thread
	LocalityShared                            // Accessed by multiple threads
	LocalityTransferred                       // Ownership transferred between threads
	LocalityUnknown                           // Not yet determined
)

func (l ThreadLocality) String() string {
	switch l {
	case LocalityThreadLocal:
		return "thread-local"
	case LocalityShared:
		return "shared"
	case LocalityTransferred:
		return "transferred"
	default:
		return "unknown"
	}
}

// ChannelOp represents a channel operation
type ChannelOp int

const (
	ChanOpNone ChannelOp = iota
	ChanOpSend           // chan-send! - transfers ownership
	ChanOpRecv           // chan-recv! - receives ownership
	ChanOpClose          // chan-close! - closes channel
)

// TransferPoint represents a point where ownership transfers between threads
type TransferPoint struct {
	ID          int            // Unique identifier
	Channel     string         // Channel variable name
	Value       string         // Value being transferred
	Op          ChannelOp      // Type of operation
	SourceLine  int            // Source location
	FromLocality ThreadLocality // Locality before transfer
	ToLocality   ThreadLocality // Locality after transfer
}

// ConcurrencyContext tracks concurrency-related ownership
type ConcurrencyContext struct {
	Localities     map[string]ThreadLocality   // Variable -> locality
	TransferPoints []*TransferPoint            // All transfer points
	Channels       map[string]bool             // Known channel variables
	InGoroutine    bool                        // Currently analyzing a goroutine
	GoroutineVars  map[string]bool             // Variables captured by goroutines
	NextID         int                         // Counter for transfer point IDs

	// For atomic refcounting decisions
	SharedObjects  map[string]bool             // Objects that need atomic RC
}

// NewConcurrencyContext creates a new concurrency analysis context
func NewConcurrencyContext() *ConcurrencyContext {
	return &ConcurrencyContext{
		Localities:     make(map[string]ThreadLocality),
		TransferPoints: make([]*TransferPoint, 0),
		Channels:       make(map[string]bool),
		GoroutineVars:  make(map[string]bool),
		SharedObjects:  make(map[string]bool),
	}
}

// MarkThreadLocal marks a variable as thread-local
func (ctx *ConcurrencyContext) MarkThreadLocal(name string) {
	if ctx.Localities[name] != LocalityShared {
		ctx.Localities[name] = LocalityThreadLocal
	}
}

// MarkShared marks a variable as shared between threads
func (ctx *ConcurrencyContext) MarkShared(name string) {
	ctx.Localities[name] = LocalityShared
	ctx.SharedObjects[name] = true
}

// MarkTransferred marks a variable as transferred to another thread
func (ctx *ConcurrencyContext) MarkTransferred(name string) {
	ctx.Localities[name] = LocalityTransferred
}

// GetLocality returns the locality of a variable
func (ctx *ConcurrencyContext) GetLocality(name string) ThreadLocality {
	if loc, ok := ctx.Localities[name]; ok {
		return loc
	}
	return LocalityUnknown
}

// RegisterChannel marks a variable as a channel
func (ctx *ConcurrencyContext) RegisterChannel(name string) {
	ctx.Channels[name] = true
	// Channels themselves are shared
	ctx.MarkShared(name)
}

// AddTransferPoint records an ownership transfer via channel
func (ctx *ConcurrencyContext) AddTransferPoint(channel, value string, op ChannelOp, line int) *TransferPoint {
	tp := &TransferPoint{
		ID:          ctx.NextID,
		Channel:     channel,
		Value:       value,
		Op:          op,
		SourceLine:  line,
		FromLocality: ctx.GetLocality(value),
	}

	// After send: sender no longer owns
	// After recv: receiver owns (fresh)
	if op == ChanOpSend {
		tp.ToLocality = LocalityTransferred
		ctx.MarkTransferred(value)
	} else if op == ChanOpRecv {
		tp.ToLocality = LocalityThreadLocal
	}

	ctx.NextID++
	ctx.TransferPoints = append(ctx.TransferPoints, tp)
	return tp
}

// CapturedByGoroutine marks a variable as captured by a goroutine
func (ctx *ConcurrencyContext) CapturedByGoroutine(name string) {
	ctx.GoroutineVars[name] = true
	// Captured variables become shared
	ctx.MarkShared(name)
}

// IsCapturedByGoroutine checks if a variable is captured by a goroutine
func (ctx *ConcurrencyContext) IsCapturedByGoroutine(name string) bool {
	return ctx.GoroutineVars[name]
}

// NeedsAtomicRC returns true if a variable needs atomic reference counting
func (ctx *ConcurrencyContext) NeedsAtomicRC(name string) bool {
	return ctx.SharedObjects[name]
}

// ConcurrencyAnalyzer analyzes concurrency patterns
type ConcurrencyAnalyzer struct {
	Ctx       *ConcurrencyContext
	ScopeVars []map[string]bool // Stack of scopes with their variables
}

// NewConcurrencyAnalyzer creates a new concurrency analyzer
func NewConcurrencyAnalyzer() *ConcurrencyAnalyzer {
	return &ConcurrencyAnalyzer{
		Ctx:       NewConcurrencyContext(),
		ScopeVars: []map[string]bool{make(map[string]bool)},
	}
}

// PushScope enters a new variable scope
func (ca *ConcurrencyAnalyzer) PushScope() {
	ca.ScopeVars = append(ca.ScopeVars, make(map[string]bool))
}

// PopScope exits a variable scope
func (ca *ConcurrencyAnalyzer) PopScope() {
	if len(ca.ScopeVars) > 1 {
		ca.ScopeVars = ca.ScopeVars[:len(ca.ScopeVars)-1]
	}
}

// AddVar adds a variable to the current scope
func (ca *ConcurrencyAnalyzer) AddVar(name string) {
	if len(ca.ScopeVars) > 0 {
		ca.ScopeVars[len(ca.ScopeVars)-1][name] = true
	}
	// New variables are thread-local by default
	ca.Ctx.MarkThreadLocal(name)
}

// IsInScope checks if a variable is in scope
func (ca *ConcurrencyAnalyzer) IsInScope(name string) bool {
	for _, scope := range ca.ScopeVars {
		if scope[name] {
			return true
		}
	}
	return false
}

// Analyze performs concurrency analysis on an expression
func (ca *ConcurrencyAnalyzer) Analyze(expr *ast.Value) {
	ca.analyzeExpr(expr, 0)
}

func (ca *ConcurrencyAnalyzer) analyzeExpr(expr *ast.Value, line int) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	switch expr.Tag {
	case ast.TCell:
		if ast.IsSym(expr.Car) {
			sym := expr.Car.Str
			switch sym {
			case "go":
				// (go expr) - spawns a goroutine
				ca.analyzeGoroutine(expr.Cdr, line)

			case "make-chan":
				// Result of make-chan is a channel
				// Handled by binding analysis

			case "chan-send!":
				// (chan-send! ch value) - transfers ownership of value
				ca.analyzeChannelSend(expr.Cdr, line)

			case "chan-recv!":
				// (chan-recv! ch) - receives ownership
				ca.analyzeChannelRecv(expr.Cdr, line)

			case "let", "let*":
				ca.analyzeLetBindings(expr, line)

			case "letrec":
				ca.analyzeLetBindings(expr, line)

			case "lambda":
				// Check for captured variables that might be shared
				ca.analyzeLambdaCaptures(expr, line)

			case "select":
				// (select clauses...) - select on multiple channels
				ca.analyzeSelect(expr.Cdr, line)

			default:
				// Regular function call - analyze arguments
				for args := expr.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
					ca.analyzeExpr(args.Car, line)
				}
			}
		} else {
			// Application of non-symbol
			ca.analyzeExpr(expr.Car, line)
			for args := expr.Cdr; !ast.IsNil(args) && ast.IsCell(args); args = args.Cdr {
				ca.analyzeExpr(args.Car, line)
			}
		}

	case ast.TSym:
		// Symbol reference - nothing to do for now
	}
}

func (ca *ConcurrencyAnalyzer) analyzeGoroutine(body *ast.Value, line int) {
	if ast.IsNil(body) || !ast.IsCell(body) {
		return
	}

	wasInGoroutine := ca.Ctx.InGoroutine
	ca.Ctx.InGoroutine = true

	// Find all free variables in the goroutine body
	freeVars := FindFreeVars(body.Car, make(map[string]bool))
	for _, v := range freeVars {
		if ca.IsInScope(v) {
			ca.Ctx.CapturedByGoroutine(v)
		}
	}

	// Analyze the body
	ca.analyzeExpr(body.Car, line)

	ca.Ctx.InGoroutine = wasInGoroutine
}

func (ca *ConcurrencyAnalyzer) analyzeChannelSend(args *ast.Value, line int) {
	if ast.IsNil(args) || !ast.IsCell(args) {
		return
	}

	// First arg is channel, second is value
	channel := ""
	if ast.IsSym(args.Car) {
		channel = args.Car.Str
	}

	if args.Cdr != nil && ast.IsCell(args.Cdr) {
		value := ""
		if ast.IsSym(args.Cdr.Car) {
			value = args.Cdr.Car.Str
		}

		if channel != "" && value != "" {
			ca.Ctx.AddTransferPoint(channel, value, ChanOpSend, line)
		}
	}
}

func (ca *ConcurrencyAnalyzer) analyzeChannelRecv(args *ast.Value, line int) {
	if ast.IsNil(args) || !ast.IsCell(args) {
		return
	}

	channel := ""
	if ast.IsSym(args.Car) {
		channel = args.Car.Str
	}

	if channel != "" {
		// Record that this is a receive point
		ca.Ctx.AddTransferPoint(channel, "", ChanOpRecv, line)
	}
}

func (ca *ConcurrencyAnalyzer) analyzeLetBindings(expr *ast.Value, line int) {
	if expr.Cdr == nil || ast.IsNil(expr.Cdr) {
		return
	}

	bindings := expr.Cdr.Car
	ca.PushScope()

	// Process bindings
	for !ast.IsNil(bindings) && ast.IsCell(bindings) {
		binding := bindings.Car
		if ast.IsCell(binding) && ast.IsSym(binding.Car) {
			varName := binding.Car.Str
			ca.AddVar(varName)

			// Check if binding is a make-chan
			if binding.Cdr != nil && ast.IsCell(binding.Cdr) {
				initExpr := binding.Cdr.Car
				if ast.IsCell(initExpr) && ast.IsSym(initExpr.Car) && initExpr.Car.Str == "make-chan" {
					ca.Ctx.RegisterChannel(varName)
				}
			}

			// Analyze the init expression
			if binding.Cdr != nil && ast.IsCell(binding.Cdr) {
				ca.analyzeExpr(binding.Cdr.Car, line)
			}
		}
		bindings = bindings.Cdr
	}

	// Analyze body
	if expr.Cdr.Cdr != nil && ast.IsCell(expr.Cdr.Cdr) {
		ca.analyzeExpr(expr.Cdr.Cdr.Car, line)
	}

	ca.PopScope()
}

func (ca *ConcurrencyAnalyzer) analyzeLambdaCaptures(expr *ast.Value, line int) {
	if expr.Cdr == nil || ast.IsNil(expr.Cdr) {
		return
	}

	// Skip params, analyze body
	if expr.Cdr.Cdr != nil && ast.IsCell(expr.Cdr.Cdr) {
		lambdaBody := expr.Cdr.Cdr.Car

		// Find free variables in lambda body
		freeVars := FindFreeVars(lambdaBody, make(map[string]bool))
		for _, v := range freeVars {
			if ca.Ctx.InGoroutine && ca.IsInScope(v) {
				// Variable captured by lambda in a goroutine
				ca.Ctx.CapturedByGoroutine(v)
			}
		}

		ca.analyzeExpr(lambdaBody, line)
	}
}

func (ca *ConcurrencyAnalyzer) analyzeSelect(clauses *ast.Value, line int) {
	// Each clause is (channel-op ...) or (default ...)
	for !ast.IsNil(clauses) && ast.IsCell(clauses) {
		clause := clauses.Car
		if ast.IsCell(clause) {
			ca.analyzeExpr(clause, line)
		}
		clauses = clauses.Cdr
	}
}

// GenerateConcurrencyInfo generates C code comments describing concurrency analysis
func (ca *ConcurrencyAnalyzer) GenerateConcurrencyInfo() string {
	var result string

	result += "/* === Concurrency Analysis Results === */\n"
	result += fmt.Sprintf("/* Transfer points: %d */\n", len(ca.Ctx.TransferPoints))
	result += fmt.Sprintf("/* Shared objects: %d */\n", len(ca.Ctx.SharedObjects))
	result += fmt.Sprintf("/* Goroutine-captured vars: %d */\n", len(ca.Ctx.GoroutineVars))

	if len(ca.Ctx.TransferPoints) > 0 {
		result += "\n/* Ownership transfer points:\n"
		for _, tp := range ca.Ctx.TransferPoints {
			opStr := "unknown"
			switch tp.Op {
			case ChanOpSend:
				opStr = "send"
			case ChanOpRecv:
				opStr = "recv"
			case ChanOpClose:
				opStr = "close"
			}
			result += fmt.Sprintf(" *   %s %s via channel %s (line %d)\n",
				opStr, tp.Value, tp.Channel, tp.SourceLine)
		}
		result += " */\n"
	}

	if len(ca.Ctx.SharedObjects) > 0 {
		result += "\n/* Objects requiring atomic reference counting:\n"
		for name := range ca.Ctx.SharedObjects {
			result += fmt.Sprintf(" *   %s\n", name)
		}
		result += " */\n"
	}

	return result
}

// ConcurrencyCodeGenerator generates concurrency-safe C code
type ConcurrencyCodeGenerator struct {
	Ctx *ConcurrencyContext
}

// NewConcurrencyCodeGenerator creates a new generator
func NewConcurrencyCodeGenerator(ctx *ConcurrencyContext) *ConcurrencyCodeGenerator {
	return &ConcurrencyCodeGenerator{Ctx: ctx}
}

// GenerateAtomicRC generates atomic reference counting operations
func (g *ConcurrencyCodeGenerator) GenerateAtomicRC() string {
	return `/* === Atomic Reference Counting for Shared Objects === */

/* Atomic increment */
static inline void atomic_inc_ref(Obj* obj) {
    if (obj) {
        __atomic_add_fetch(&obj->mark, 1, __ATOMIC_SEQ_CST);
    }
}

/* Atomic decrement with potential free */
static inline void atomic_dec_ref(Obj* obj) {
    if (obj) {
        if (__atomic_sub_fetch(&obj->mark, 1, __ATOMIC_SEQ_CST) == 0) {
            free_obj(obj);
        }
    }
}

/* Try to acquire unique ownership (for in-place updates) */
static inline bool try_acquire_unique(Obj* obj) {
    if (!obj) return false;
    int expected = 1;
    return __atomic_compare_exchange_n(&obj->mark, &expected, 1,
        false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
`
}

// GenerateChannelRuntime generates channel operations
func (g *ConcurrencyCodeGenerator) GenerateChannelRuntime() string {
	return `/* === Channel Operations with Ownership Transfer === */

typedef struct Channel Channel;
struct Channel {
    Obj** buffer;       /* Ring buffer for values */
    int capacity;       /* Buffer size (0 = unbuffered) */
    int count;          /* Current number of items */
    int read_pos;       /* Read position */
    int write_pos;      /* Write position */
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    bool closed;
};

/* Create a channel */
static Obj* make_channel(int capacity) {
    Channel* ch = malloc(sizeof(Channel));
    if (!ch) return NULL;

    ch->capacity = capacity > 0 ? capacity : 1;
    ch->buffer = malloc(sizeof(Obj*) * ch->capacity);
    ch->count = 0;
    ch->read_pos = 0;
    ch->write_pos = 0;
    ch->closed = false;

    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);

    Obj* obj = malloc(sizeof(Obj));
    if (!obj) {
        free(ch->buffer);
        free(ch);
        return NULL;
    }
    obj->mark = 1;
    obj->scc_id = -1;
    obj->is_pair = 0;
    obj->scan_tag = 0;
    obj->tag = TAG_CHANNEL;
    obj->gen_obj = NULL;
    obj->ptr = ch;

    return obj;
}

static Channel* channel_payload(Obj* ch_obj) {
    if (!ch_obj || ch_obj->tag != TAG_CHANNEL) return NULL;
    return (Channel*)ch_obj->ptr;
}

/* Send value through channel (TRANSFERS OWNERSHIP) */
/* After send, caller should NOT use or free the value */
static bool channel_send(Obj* ch_obj, Obj* value) {
    Channel* ch = channel_payload(ch_obj);
    if (!ch || ch->closed) return false;

    pthread_mutex_lock(&ch->lock);

    /* Wait for space */
    while (ch->count >= ch->capacity && !ch->closed) {
        pthread_cond_wait(&ch->not_full, &ch->lock);
    }

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return false;
    }

    /* Transfer ownership: no inc_ref needed, sender gives up value */
    ch->buffer[ch->write_pos] = value;
    ch->write_pos = (ch->write_pos + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->lock);

    return true;
}

/* Receive value from channel (RECEIVES OWNERSHIP) */
/* Caller becomes owner, must free when done */
static Obj* channel_recv(Obj* ch_obj) {
    Channel* ch = channel_payload(ch_obj);
    if (!ch) return NULL;

    pthread_mutex_lock(&ch->lock);

    /* Wait for data */
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->lock);
    }

    if (ch->count == 0) {
        /* Channel closed and empty */
        pthread_mutex_unlock(&ch->lock);
        return NULL;
    }

    /* Transfer ownership: receiver now owns the value */
    Obj* value = ch->buffer[ch->read_pos];
    ch->read_pos = (ch->read_pos + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);

    return value;  /* Caller owns this */
}

/* Close a channel */
static void channel_close(Obj* ch_obj) {
    Channel* ch = channel_payload(ch_obj);
    if (!ch) return;

    pthread_mutex_lock(&ch->lock);
    ch->closed = true;
    pthread_cond_broadcast(&ch->not_empty);
    pthread_cond_broadcast(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);
}

/* Free a channel */
static void free_channel_obj(Obj* ch_obj) {
    Channel* ch = channel_payload(ch_obj);
    if (!ch) return;

    /* Free any remaining values (ownership cleanup) */
    while (ch->count > 0) {
        Obj* val = ch->buffer[ch->read_pos];
        if (val) dec_ref(val);
        ch->read_pos = (ch->read_pos + 1) % ch->capacity;
        ch->count--;
    }

    free(ch->buffer);
    pthread_mutex_destroy(&ch->lock);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch);
    if (ch_obj) {
        ch_obj->ptr = NULL;
    }
}
`
}

// GenerateGoroutineRuntime generates goroutine spawning code
func (g *ConcurrencyCodeGenerator) GenerateGoroutineRuntime() string {
	return `/* === Goroutine Spawning === */

typedef struct GoroutineArg GoroutineArg;
struct GoroutineArg {
    Obj* closure;       /* The closure to run */
    Obj** captured;     /* Captured variables (with inc_ref'd ownership) */
    int captured_count;
};

/* Thread entry point */
static void* goroutine_entry(void* arg) {
    GoroutineArg* ga = (GoroutineArg*)arg;

    /* Call the closure */
    if (ga->closure) {
        call_closure(ga->closure, NULL, 0);
        dec_ref(ga->closure);
    }

    /* Release captured variables */
    for (int i = 0; i < ga->captured_count; i++) {
        if (ga->captured[i]) {
            atomic_dec_ref(ga->captured[i]);
        }
    }

    free(ga->captured);
    free(ga);
    return NULL;
}

/* Spawn a goroutine */
static void spawn_goroutine(Obj* closure, Obj** captured, int count) {
    GoroutineArg* arg = malloc(sizeof(GoroutineArg));
    if (!arg) return;

    /* Transfer ownership of closure to goroutine */
    arg->closure = closure;
    inc_ref(closure);

    /* Copy and increment captured variables (they become shared) */
    arg->captured_count = count;
    arg->captured = malloc(sizeof(Obj*) * count);
    for (int i = 0; i < count; i++) {
        arg->captured[i] = captured[i];
        if (captured[i]) {
            atomic_inc_ref(captured[i]);
        }
    }

    pthread_t thread;
    pthread_create(&thread, NULL, goroutine_entry, arg);
    pthread_detach(thread);  /* Don't wait for completion */
}
`
}

// GenerateAtomRuntime generates Clojure-style atom operations
func (g *ConcurrencyCodeGenerator) GenerateAtomRuntime() string {
	return `/* === Atom (Atomic Reference) Operations === */

typedef struct Atom Atom;
struct Atom {
    Obj* value;
    pthread_mutex_t lock;
};

/* Create an atom */
static Obj* make_atom(Obj* initial) {
    Atom* a = malloc(sizeof(Atom));
    if (!a) return NULL;

    a->value = initial;
    if (initial) inc_ref(initial);
    pthread_mutex_init(&a->lock, NULL);

    Obj* obj = malloc(sizeof(Obj));
    if (!obj) {
        if (initial) dec_ref(initial);
        free(a);
        return NULL;
    }
    obj->mark = 1;
    obj->scc_id = -1;
    obj->is_pair = 0;
    obj->scan_tag = 0;
    obj->tag = TAG_ATOM;
    obj->gen_obj = NULL;
    obj->ptr = a;

    return obj;
}

static Atom* atom_payload(Obj* atom_obj) {
    if (!atom_obj || atom_obj->tag != TAG_ATOM) return NULL;
    return (Atom*)atom_obj->ptr;
}

/* Dereference atom (read current value) */
static Obj* atom_deref(Obj* atom_obj) {
    Atom* a = atom_payload(atom_obj);
    if (!a) return NULL;

    pthread_mutex_lock(&a->lock);
    Obj* val = a->value;
    if (val) inc_ref(val);
    pthread_mutex_unlock(&a->lock);

    return val;
}

/* Reset atom to new value */
static Obj* atom_reset(Obj* atom_obj, Obj* new_val) {
    Atom* a = atom_payload(atom_obj);
    if (!a) return NULL;

    pthread_mutex_lock(&a->lock);
    Obj* old = a->value;
    a->value = new_val;
    if (new_val) inc_ref(new_val);
    if (old) dec_ref(old);
    pthread_mutex_unlock(&a->lock);

    if (new_val) inc_ref(new_val);
    return new_val;
}

/* Swap atom value using function */
static Obj* atom_swap(Obj* atom_obj, Obj* fn) {
    Atom* a = atom_payload(atom_obj);
    if (!a || !fn) return NULL;

    pthread_mutex_lock(&a->lock);
    Obj* old = a->value;

    /* Call function with current value */
    Obj* args[1] = { old };
    Obj* new_val = call_closure(fn, args, 1);

    a->value = new_val;
    if (old) dec_ref(old);

    pthread_mutex_unlock(&a->lock);

    if (new_val) inc_ref(new_val);
    return new_val;
}

/* Compare-and-set */
static Obj* atom_cas(Obj* atom_obj, Obj* expected, Obj* new_val) {
    Atom* a = atom_payload(atom_obj);
    if (!a) return mk_int(0);

    pthread_mutex_lock(&a->lock);
    bool success = (a->value == expected);
    if (success) {
        Obj* old = a->value;
        a->value = new_val;
        if (new_val) inc_ref(new_val);
        if (old) dec_ref(old);
    }
    pthread_mutex_unlock(&a->lock);

    return mk_int(success ? 1 : 0);
}

/* Free atom */
static void free_atom_obj(Obj* atom_obj) {
    Atom* a = atom_payload(atom_obj);
    if (!a) return;

    if (a->value) dec_ref(a->value);
    pthread_mutex_destroy(&a->lock);
    free(a);
    if (atom_obj) atom_obj->ptr = NULL;
}
`
}

// GenerateThreadRuntime generates thread spawning with join support
func (g *ConcurrencyCodeGenerator) GenerateThreadRuntime() string {
	return `/* === Thread Operations (with join support) === */

typedef struct ThreadHandle ThreadHandle;
struct ThreadHandle {
    pthread_t thread;
    Obj* result;
    bool done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

typedef struct ThreadArg ThreadArg;
struct ThreadArg {
    Obj* closure;
    ThreadHandle* handle;
};

/* Thread entry point */
static void* thread_entry(void* arg) {
    ThreadArg* ta = (ThreadArg*)arg;

    /* Call the closure */
    Obj* result = NULL;
    if (ta->closure) {
        result = call_closure(ta->closure, NULL, 0);
        dec_ref(ta->closure);
    }

    /* Store result and signal completion */
    pthread_mutex_lock(&ta->handle->lock);
    ta->handle->result = result;
    ta->handle->done = true;
    pthread_cond_signal(&ta->handle->cond);
    pthread_mutex_unlock(&ta->handle->lock);

    free(ta);
    return NULL;
}

/* Spawn a thread (returns handle for joining) */
static Obj* spawn_thread(Obj* closure) {
    ThreadHandle* h = malloc(sizeof(ThreadHandle));
    if (!h) return NULL;

    h->result = NULL;
    h->done = false;
    pthread_mutex_init(&h->lock, NULL);
    pthread_cond_init(&h->cond, NULL);

    ThreadArg* arg = malloc(sizeof(ThreadArg));
    if (!arg) {
        free(h);
        return NULL;
    }

    arg->closure = closure;
    if (closure) inc_ref(closure);
    arg->handle = h;

    pthread_create(&h->thread, NULL, thread_entry, arg);

    /* Wrap handle in Obj */
    Obj* obj = malloc(sizeof(Obj));
    if (!obj) return NULL;
    obj->mark = 1;
    obj->scc_id = -1;
    obj->is_pair = 0;
    obj->scan_tag = 0;
    obj->tag = TAG_THREAD;
    obj->gen_obj = NULL;
    obj->ptr = h;

    return obj;
}

static ThreadHandle* thread_payload(Obj* thread_obj) {
    if (!thread_obj || thread_obj->tag != TAG_THREAD) return NULL;
    return (ThreadHandle*)thread_obj->ptr;
}

/* Join thread and get result */
static Obj* thread_join(Obj* thread_obj) {
    ThreadHandle* h = thread_payload(thread_obj);
    if (!h) return NULL;

    pthread_mutex_lock(&h->lock);
    while (!h->done) {
        pthread_cond_wait(&h->cond, &h->lock);
    }
    Obj* result = h->result;
    if (result) inc_ref(result);
    pthread_mutex_unlock(&h->lock);

    return result;
}

/* Free thread handle */
static void free_thread_obj(Obj* thread_obj) {
    ThreadHandle* h = thread_payload(thread_obj);
    if (!h) return;

    pthread_join(h->thread, NULL);
    if (h->result) dec_ref(h->result);
    pthread_mutex_destroy(&h->lock);
    pthread_cond_destroy(&h->cond);
    free(h);
    if (thread_obj) thread_obj->ptr = NULL;
}
`
}

// GenerateConcurrencyRuntime generates all concurrency runtime code
func (g *ConcurrencyCodeGenerator) GenerateConcurrencyRuntime() string {
	return `/* ========== Concurrency Runtime ========== */
/* Thread-safe ownership management with message passing */

#include <pthread.h>
#include <stdbool.h>

` + g.GenerateAtomicRC() + "\n" + g.GenerateChannelRuntime() + "\n" + g.GenerateGoroutineRuntime() + "\n" + g.GenerateAtomRuntime() + "\n" + g.GenerateThreadRuntime()
}
