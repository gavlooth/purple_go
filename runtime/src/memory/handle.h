/*
 * Handle-Based Borrowed References
 *
 * Provides sound use-after-free detection using the stable slot pool.
 * All validation operations read from stable memory (never freed).
 *
 * Usage:
 *   // Allocate object
 *   Obj* obj = handle_alloc_obj();
 *
 *   // Create handle for sharing
 *   Handle h = handle_from_obj(obj);
 *
 *   // Validate and access
 *   if (handle_is_valid(h)) {
 *       Obj* ptr = handle_deref_obj(h);
 *       // use ptr...
 *   }
 *
 *   // Free object (invalidates handles)
 *   handle_free_obj(obj);
 */

#ifndef PURPLE_HANDLE_H
#define PURPLE_HANDLE_H

#include "slot_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Forward Declarations ============== */

/* Obj is defined in runtime.c/purple.h */
#ifndef PURPLE_OBJ_DEFINED
struct Obj;
typedef struct Obj Obj;
#endif

/* ============== Handle-Based Obj Allocation ============== */

/*
 * Allocate an Obj from the stable slot pool.
 * Returns pointer to Obj (which is the slot payload).
 * The Obj will have a valid generation set.
 */
Obj* handle_alloc_obj(void);

/*
 * Free an Obj back to the slot pool.
 * The slot's generation is incremented, invalidating handles.
 * The memory is NOT freed to system - stays in pool.
 */
void handle_free_obj(Obj* obj);

/*
 * Check if an Obj was allocated from the slot pool.
 */
bool handle_is_pool_obj(Obj* obj);

/*
 * Create a handle from an Obj.
 * The handle embeds a validation tag in unused pointer bits.
 */
Handle handle_from_obj(Obj* obj);

/*
 * Dereference a handle to get the Obj.
 * Returns NULL if handle is invalid (stale/freed).
 * This is the SAFE access path.
 */
Obj* handle_deref_obj(Handle h);

/*
 * Validate a handle without dereferencing.
 * Use handle_is_valid() from slot_pool.h - it's inline and fast.
 */

/* ============== Borrowed Reference Type ============== */

/*
 * HandleRef: A borrowed reference with debug info.
 * Wraps a Handle with source description for debugging.
 */
typedef struct HandleRef {
    Handle handle;
    const char* source_desc;  /* Debug: "loop iterator", "callback arg", etc. */
} HandleRef;

/*
 * Create a HandleRef from an Obj.
 */
static inline HandleRef handleref_create(Obj* obj, const char* desc) {
    HandleRef ref;
    ref.handle = handle_from_obj(obj);
    ref.source_desc = desc;
    return ref;
}

/*
 * Check if a HandleRef is valid.
 */
static inline bool handleref_is_valid(HandleRef* ref) {
    return ref && handle_is_valid(ref->handle);
}

/*
 * Dereference a HandleRef.
 */
static inline Obj* handleref_deref(HandleRef* ref) {
    if (!ref) return NULL;
    return handle_deref_obj(ref->handle);
}

/* ============== Integration with Legacy BorrowRef ============== */

/*
 * These functions bridge the new Handle system with the legacy BorrowRef.
 * Eventually BorrowRef should be deprecated in favor of HandleRef.
 */

struct BorrowRef;

/*
 * Create a BorrowRef using the handle system.
 * The BorrowRef will validate via handles instead of direct memory access.
 */
struct BorrowRef* handle_borrow_create(Obj* obj, const char* source_desc);

/*
 * Validate a BorrowRef using the handle system.
 */
bool handle_borrow_is_valid(struct BorrowRef* ref);

/*
 * Get Obj from BorrowRef using handle validation.
 */
Obj* handle_borrow_get(struct BorrowRef* ref);

/* ============== Pool Management ============== */

/*
 * Initialize the handle system.
 * Called automatically on first use, but can be called explicitly.
 */
void handle_system_init(void);

/*
 * Shutdown the handle system.
 * Call at program exit for clean shutdown.
 */
void handle_system_shutdown(void);

/*
 * Get statistics about handle allocations.
 */
void handle_get_stats(SlotPoolStats* stats);

#ifdef __cplusplus
}
#endif

#endif /* PURPLE_HANDLE_H */
