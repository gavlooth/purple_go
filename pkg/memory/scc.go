package memory

import (
	"fmt"
	"io"
)

// SCCGenerator generates SCC-based reference counting code
// Based on ISMM 2024: For deeply immutable (frozen) cyclic structures
type SCCGenerator struct {
	w io.Writer
}

// NewSCCGenerator creates a new SCC generator
func NewSCCGenerator(w io.Writer) *SCCGenerator {
	return &SCCGenerator{w: w}
}

func (g *SCCGenerator) emit(format string, args ...interface{}) {
	fmt.Fprintf(g.w, format, args...)
}

// GenerateSCCRuntime generates the SCC runtime support code
func (g *SCCGenerator) GenerateSCCRuntime() {
	g.emit(`/* SCC-Based Reference Counting (ISMM 2024) */
/* For frozen (deeply immutable) cyclic structures */

typedef struct SCC {
    int id;
    Obj** members;
    int member_count;
    int member_capacity;
    int ref_count;      /* Single RC for entire SCC */
    int frozen;         /* 1 if immutable */
    struct SCC* next;
} SCC;

typedef struct SCCRegistry {
    SCC* sccs;
    int next_id;
} SCCRegistry;

SCCRegistry SCC_REGISTRY = {NULL, 0};

/* Tarjan's Algorithm state */
typedef struct TarjanState {
    int* index;
    int* lowlink;
    int* on_stack;
    Obj** stack;
    int stack_top;
    int current_index;
    int capacity;
} TarjanState;

static TarjanState* tarjan_init(int capacity) {
    TarjanState* s = malloc(sizeof(TarjanState));
    if (!s) return NULL;
    s->index = calloc(capacity, sizeof(int));
    s->lowlink = calloc(capacity, sizeof(int));
    s->on_stack = calloc(capacity, sizeof(int));
    s->stack = malloc(capacity * sizeof(Obj*));
    s->stack_top = 0;
    s->current_index = 1;
    s->capacity = capacity;
    if (!s->index || !s->lowlink || !s->on_stack || !s->stack) {
        free(s->index);
        free(s->lowlink);
        free(s->on_stack);
        free(s->stack);
        free(s);
        return NULL;
    }
    return s;
}

static void tarjan_free(TarjanState* s) {
    if (!s) return;
    free(s->index);
    free(s->lowlink);
    free(s->on_stack);
    free(s->stack);
    free(s);
}

SCC* create_scc(void) {
    SCC* scc = malloc(sizeof(SCC));
    if (!scc) return NULL;
    scc->id = SCC_REGISTRY.next_id++;
    scc->members = malloc(16 * sizeof(Obj*));
    scc->member_count = 0;
    scc->member_capacity = 16;
    scc->ref_count = 1;
    scc->frozen = 0;
    scc->next = SCC_REGISTRY.sccs;
    SCC_REGISTRY.sccs = scc;
    return scc;
}

void scc_add_member(SCC* scc, Obj* obj) {
    if (!scc || !obj) return;
    if (scc->member_count >= scc->member_capacity) {
        int new_cap = scc->member_capacity * 2;
        Obj** new_members = realloc(scc->members, new_cap * sizeof(Obj*));
        if (!new_members) return;
        scc->members = new_members;
        scc->member_capacity = new_cap;
    }
    scc->members[scc->member_count++] = obj;
    obj->scc_id = scc->id;
}

void freeze_scc(SCC* scc) {
    if (scc) scc->frozen = 1;
}

void release_scc(SCC* scc) {
    if (!scc) return;
    scc->ref_count--;
    if (scc->ref_count <= 0 && scc->frozen) {
        /* Free all members */
        for (int i = 0; i < scc->member_count; i++) {
            Obj* obj = scc->members[i];
            if (obj) {
                invalidate_weak_refs_for(obj);
                free(obj);
            }
        }
        free(scc->members);

        /* Remove from registry */
        SCC** pp = &SCC_REGISTRY.sccs;
        while (*pp) {
            if (*pp == scc) {
                *pp = scc->next;
                break;
            }
            pp = &(*pp)->next;
        }
        free(scc);
    }
}

SCC* find_scc(int id) {
    SCC* scc = SCC_REGISTRY.sccs;
    while (scc) {
        if (scc->id == id) return scc;
        scc = scc->next;
    }
    return NULL;
}

/* Release object considering SCC membership */
void release_with_scc(Obj* obj) {
    if (!obj) return;
    if (obj->scc_id >= 0) {
        SCC* scc = find_scc(obj->scc_id);
        if (scc) {
            release_scc(scc);
            return;
        }
    }
    dec_ref(obj);
}

`)
}

// GenerateSCCDetection generates code to detect and create SCCs
func (g *SCCGenerator) GenerateSCCDetection() {
	g.emit(`/* SCC Detection (Tarjan's Algorithm) */
/* Called after construction of potentially cyclic structure */

static void tarjan_strongconnect(Obj* v, TarjanState* state,
                                  void (*on_scc)(Obj**, int)) {
    if (!v || !state) return;

    /* Use scan_tag field to store Tarjan index for this node */
    int v_idx = state->current_index++;
    v->scan_tag = (unsigned int)v_idx;  /* Store index in node */
    state->index[v_idx %% state->capacity] = v_idx;
    state->lowlink[v_idx %% state->capacity] = v_idx;
    state->stack[state->stack_top++] = v;
    state->on_stack[v_idx %% state->capacity] = 1;

    /* Visit children */
    if (v->is_pair) {
        Obj* children[] = {v->a, v->b};
        for (int i = 0; i < 2; i++) {
            Obj* w = children[i];
            if (!w) continue;

            /* Check if w has been visited (scan_tag != 0 means visited) */
            int w_idx = (int)w->scan_tag;
            if (w_idx == 0) {
                /* Not visited yet */
                tarjan_strongconnect(w, state, on_scc);
                /* Update lowlink */
                int w_low = state->lowlink[w->scan_tag %% state->capacity];
                if (w_low < state->lowlink[v_idx %% state->capacity]) {
                    state->lowlink[v_idx %% state->capacity] = w_low;
                }
            } else if (state->on_stack[w_idx %% state->capacity]) {
                /* w is on stack, update lowlink */
                if (state->index[w_idx %% state->capacity] < state->lowlink[v_idx %% state->capacity]) {
                    state->lowlink[v_idx %% state->capacity] = state->index[w_idx %% state->capacity];
                }
            }
        }
    }

    /* Check if v is root of SCC */
    if (state->lowlink[v_idx %% state->capacity] == state->index[v_idx %% state->capacity]) {
        /* Pop SCC from stack */
        Obj* scc_members[256];
        int scc_size = 0;
        Obj* w;
        do {
            w = state->stack[--state->stack_top];
            int w_idx = (int)w->scan_tag;
            state->on_stack[w_idx %% state->capacity] = 0;
            if (scc_size < 256) {
                scc_members[scc_size++] = w;
            }
        } while (w != v && state->stack_top > 0);

        if (scc_size > 1 && on_scc) {
            on_scc(scc_members, scc_size);
        }
    }
}

static void on_scc_found(Obj** members, int count) {
    SCC* scc = create_scc();
    if (!scc) return;
    for (int i = 0; i < count; i++) {
        scc_add_member(scc, members[i]);
    }
}

void detect_and_freeze_sccs(Obj* root) {
    TarjanState* state = tarjan_init(1024);
    if (!state) return;
    tarjan_strongconnect(root, state, on_scc_found);

    /* Freeze all detected SCCs */
    SCC* scc = SCC_REGISTRY.sccs;
    while (scc) {
        if (!scc->frozen) {
            freeze_scc(scc);
        }
        scc = scc->next;
    }

    tarjan_free(state);
}

`)
}
