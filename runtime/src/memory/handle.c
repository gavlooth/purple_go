/*
 * Handle-Based Borrowed References Implementation
 */

#include "handle.h"
#include "slot_pool.h"
#include <stdlib.h>
#include <string.h>

/* ============== Obj Type Definition ============== */

/*
 * We need to know the Obj structure to work with it.
 * This is a forward declaration - actual definition is in runtime.c.
 * We only need to know the size for slot allocation.
 */

#ifndef PURPLE_OBJ_SIZE
#define PURPLE_OBJ_SIZE 48
#endif

/* Minimal Obj structure for generation access */
typedef struct MinimalObj {
    uint16_t generation;  /* Or uint64_t in robust mode */
    int mark;
    int tag;
    /* ... rest of fields ... */
} MinimalObj;

/* ============== Global State ============== */

static SlotPool* g_obj_pool = NULL;
static bool g_initialized = false;

/* ============== Initialization ============== */

void handle_system_init(void) {
    if (g_initialized) return;

    g_obj_pool = slot_pool_create(PURPLE_OBJ_SIZE, 16, SLOT_POOL_BLOCK_SIZE);
    g_initialized = true;
}

void handle_system_shutdown(void) {
    if (!g_initialized) return;

    slot_pool_destroy(g_obj_pool);
    g_obj_pool = NULL;
    g_initialized = false;
}

static inline SlotPool* get_pool(void) {
    if (!g_initialized) {
        handle_system_init();
    }
    return g_obj_pool;
}

/* ============== Pool Bounds Checking ============== */

/*
 * Check if a pointer is within the slot pool.
 * Used to determine if an Obj came from the pool or malloc.
 */
typedef struct PoolBounds {
    uintptr_t start;
    uintptr_t end;
    struct PoolBounds* next;
} PoolBounds;

static PoolBounds* g_pool_bounds = NULL;

static void update_pool_bounds(void) {
    /* Free old bounds */
    while (g_pool_bounds) {
        PoolBounds* next = g_pool_bounds->next;
        free(g_pool_bounds);
        g_pool_bounds = next;
    }

    /* Build new bounds from pool blocks */
    SlotPool* pool = get_pool();
    if (!pool) return;

    for (SlotBlock* block = pool->blocks; block; block = block->next) {
        PoolBounds* bounds = malloc(sizeof(PoolBounds));
        if (!bounds) continue;

        bounds->start = (uintptr_t)block->slots;
        bounds->end = bounds->start + block->slot_count * block->slot_stride;
        bounds->next = g_pool_bounds;
        g_pool_bounds = bounds;
    }
}

bool handle_is_pool_obj(Obj* obj) {
    if (!obj) return false;

    uintptr_t ptr = (uintptr_t)obj;

    /* Check against known bounds */
    for (PoolBounds* b = g_pool_bounds; b; b = b->next) {
        if (ptr >= b->start && ptr < b->end) {
            return true;
        }
    }

    /* Bounds might be stale - update and retry */
    update_pool_bounds();

    for (PoolBounds* b = g_pool_bounds; b; b = b->next) {
        if (ptr >= b->start && ptr < b->end) {
            return true;
        }
    }

    return false;
}

/* ============== Obj Allocation ============== */

Obj* handle_alloc_obj(void) {
    SlotPool* pool = get_pool();
    if (!pool) return NULL;

    Slot* slot = slot_pool_alloc(pool);
    if (!slot) return NULL;

    /* Get payload (Obj*) */
    Obj* obj = (Obj*)slot_payload(slot);

    /* Initialize minimal fields
     * The caller will set the rest (tag, value, etc.)
     */
    memset(obj, 0, PURPLE_OBJ_SIZE);

    /* Copy generation from slot to obj for IPGE compatibility */
    MinimalObj* mobj = (MinimalObj*)obj;
    mobj->generation = (uint16_t)slot->generation;
    mobj->mark = 1;  /* Initial refcount */

    return obj;
}

void handle_free_obj(Obj* obj) {
    if (!obj) return;

    /* Check if from pool */
    if (!handle_is_pool_obj(obj)) {
        /* Not from pool - use regular free */
        /* This maintains backward compatibility */
        return;
    }

    /* Get slot from obj */
    Slot* slot = slot_from_payload(obj);

    /* Free to pool */
    SlotPool* pool = get_pool();
    slot_pool_free(pool, slot);
}

/* ============== Handle Operations ============== */

Handle handle_from_obj(Obj* obj) {
    if (!obj) return HANDLE_INVALID;

    /* Check if from pool */
    if (!handle_is_pool_obj(obj)) {
        /* Not from pool - can't create safe handle
         * Return a pseudo-handle that will fail validation
         * This is for backward compatibility with non-pool objects
         */
        return HANDLE_INVALID;
    }

    Slot* slot = slot_from_payload(obj);
    SlotPool* pool = get_pool();
    return slot_pool_make_handle(pool, slot);
}

Obj* handle_deref_obj(Handle h) {
    if (!handle_is_valid(h)) {
        return NULL;
    }

    Slot* slot = handle_to_slot(h);
    return (Obj*)slot_payload(slot);
}

/* ============== BorrowRef Integration ============== */

/*
 * BorrowRef structure - must match runtime.c definition
 */
typedef struct BorrowRef {
    void* target;            /* Legacy GenObj system */
    uint16_t remembered_gen; /* Snapshot of generation */
    const char* source_desc;
    void* ipge_target;       /* IPGE: Direct Obj* */
    Handle handle;           /* NEW: Stable handle */
} BorrowRef;

struct BorrowRef* handle_borrow_create(Obj* obj, const char* source_desc) {
    BorrowRef* ref = malloc(sizeof(BorrowRef));
    if (!ref) return NULL;

    ref->target = NULL;
    ref->remembered_gen = 0;
    ref->source_desc = source_desc;
    ref->ipge_target = obj;
    ref->handle = handle_from_obj(obj);

    return ref;
}

bool handle_borrow_is_valid(struct BorrowRef* ref) {
    if (!ref) return false;

    /* If we have a valid handle, use it */
    if (ref->handle != HANDLE_INVALID) {
        return handle_is_valid(ref->handle);
    }

    /* Fallback to legacy IPGE validation
     * WARNING: This may read freed memory if not using pool! */
    Obj* obj = (Obj*)ref->ipge_target;
    if (!obj) return false;

    MinimalObj* mobj = (MinimalObj*)obj;
    return mobj->generation == ref->remembered_gen;
}

Obj* handle_borrow_get(struct BorrowRef* ref) {
    if (!handle_borrow_is_valid(ref)) {
        return NULL;
    }

    /* If we have a valid handle, use it */
    if (ref->handle != HANDLE_INVALID) {
        return handle_deref_obj(ref->handle);
    }

    /* Fallback to direct pointer */
    return (Obj*)ref->ipge_target;
}

/* ============== Statistics ============== */

void handle_get_stats(SlotPoolStats* stats) {
    SlotPool* pool = get_pool();
    if (pool && stats) {
        slot_pool_get_stats(pool, stats);
    }
}
