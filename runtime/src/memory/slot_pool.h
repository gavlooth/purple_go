/*
 * Stable Slot Pool for Sound Generational References
 *
 * This module provides a pool allocator where slots are NEVER freed back to
 * the system allocator. This makes generational validation sound - reading
 * slot->generation is always defined behavior, even after logical free.
 *
 * Key Properties:
 * 1. Slots are pre-allocated in blocks
 * 2. "Free" returns slot to internal freelist, not to malloc
 * 3. Generation field remains readable forever
 * 4. Cached tag in slot header for fast validation
 *
 * Reference: docs/GENERATIONAL_MEMORY.md (Approach 3: Cached-Tag Handles)
 */

#ifndef PURPLE_SLOT_POOL_H
#define PURPLE_SLOT_POOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Configuration ============== */

/* Initial pool size (slots per block) */
#ifndef SLOT_POOL_BLOCK_SIZE
#define SLOT_POOL_BLOCK_SIZE 4096
#endif

/* Maximum total slots (can grow dynamically) */
#ifndef SLOT_POOL_MAX_BLOCKS
#define SLOT_POOL_MAX_BLOCKS 256
#endif

/* ============== Slot Flags ============== */

#define SLOT_FREE    0u
#define SLOT_IN_USE  1u

/* ============== Handle Type ============== */

/*
 * Handle is a tagged pointer:
 * - AArch64: top 8 bits = tag, bottom 56 bits = pointer
 * - x86-64/Other: bottom 4 bits = tag (requires 16-byte alignment)
 */
typedef uintptr_t Handle;

#define HANDLE_INVALID 0

/* Platform detection */
#if defined(__aarch64__)
  #define HANDLE_USE_TOP_BYTE 1
  #define HANDLE_TAG_SHIFT 56
  #define HANDLE_TAG_MASK  ((uintptr_t)0xFFu << HANDLE_TAG_SHIFT)
  #define HANDLE_PTR_MASK  (~HANDLE_TAG_MASK)
  #define HANDLE_TAG_BITS  8
#else
  #define HANDLE_USE_TOP_BYTE 0
  #define HANDLE_TAG_BITS 4
  #define HANDLE_TAG_MASK ((uintptr_t)((1u << HANDLE_TAG_BITS) - 1u))
  #define HANDLE_PTR_MASK (~HANDLE_TAG_MASK)
#endif

/* ============== Slot Structure ============== */

/*
 * Slot header - 16 bytes, followed by payload.
 * The header is ALWAYS readable, even after logical free.
 */
typedef struct Slot {
    uint32_t generation;     /* Increments on each alloc/free cycle */
    uint8_t  tag8;           /* Cached validation tag */
    uint8_t  flags;          /* SLOT_FREE or SLOT_IN_USE */
    uint16_t size_class;     /* Size class index (for multi-size pools) */
    /* Payload follows immediately after header */
} Slot;

/* Get payload pointer from slot */
static inline void* slot_payload(Slot* s) {
    return (void*)(s + 1);
}

/* Get slot from payload pointer */
static inline Slot* slot_from_payload(void* payload) {
    return ((Slot*)payload) - 1;
}

/* ============== Slot Pool Structure ============== */

typedef struct SlotBlock {
    Slot* slots;             /* Array of slots */
    size_t slot_count;       /* Number of slots in this block */
    size_t slot_stride;      /* Bytes per slot (header + payload) */
    struct SlotBlock* next;  /* Next block in chain */
} SlotBlock;

typedef struct SlotPool {
    SlotBlock* blocks;       /* Linked list of blocks */
    size_t block_count;      /* Number of blocks */

    /* Freelist (indices into flattened slot array) */
    Slot** freelist;         /* Array of free slot pointers */
    size_t freelist_capacity;
    size_t freelist_top;     /* Stack pointer */

    /* Configuration */
    size_t payload_size;     /* Size of payload per slot */
    size_t slot_stride;      /* Total bytes per slot */
    size_t alignment;        /* Required alignment */

    /* Secret for tag computation */
    uint64_t secret;

    /* Statistics */
    size_t total_allocs;
    size_t total_frees;
    size_t current_in_use;
    size_t peak_in_use;
} SlotPool;

/* ============== Slot Pool API ============== */

/*
 * Create a new slot pool.
 *
 * @param payload_size  Size of payload per slot (e.g., sizeof(Obj))
 * @param alignment     Required alignment (usually 8 or 16)
 * @param initial_slots Initial number of slots to pre-allocate
 * @return              New pool, or NULL on failure
 */
SlotPool* slot_pool_create(size_t payload_size, size_t alignment, size_t initial_slots);

/*
 * Destroy a slot pool and release all memory.
 * WARNING: All handles become invalid after this!
 */
void slot_pool_destroy(SlotPool* pool);

/*
 * Allocate a slot from the pool.
 *
 * @param pool  The slot pool
 * @return      Pointer to slot, or NULL if pool exhausted and can't grow
 */
Slot* slot_pool_alloc(SlotPool* pool);

/*
 * Free a slot back to the pool.
 * The slot's generation is incremented, invalidating existing handles.
 * The slot memory is NOT freed - it stays in the pool.
 *
 * @param pool  The slot pool
 * @param slot  The slot to free
 */
void slot_pool_free(SlotPool* pool, Slot* slot);

/*
 * Create a handle from a slot.
 * The handle embeds a validation tag for fast checking.
 *
 * @param pool  The slot pool (for secret)
 * @param slot  The slot to create handle for
 * @return      Handle value
 */
Handle slot_pool_make_handle(SlotPool* pool, Slot* slot);

/*
 * Extract slot pointer from handle.
 * Does NOT validate - use handle_is_valid first!
 *
 * @param h  The handle
 * @return   Slot pointer (may be invalid if handle is stale)
 */
static inline Slot* handle_to_slot(Handle h) {
    if (h == HANDLE_INVALID) return NULL;
    return (Slot*)(h & HANDLE_PTR_MASK);
}

/*
 * Extract tag from handle.
 *
 * @param h  The handle
 * @return   Tag value
 */
static inline uint8_t handle_get_tag(Handle h) {
#if HANDLE_USE_TOP_BYTE
    return (uint8_t)(h >> HANDLE_TAG_SHIFT);
#else
    return (uint8_t)(h & HANDLE_TAG_MASK);
#endif
}

/*
 * Validate a handle.
 * This is the FAST PATH - just loads and compares, no computation.
 *
 * @param h  The handle to validate
 * @return   true if valid, false if stale/invalid
 */
static inline bool handle_is_valid(Handle h) {
    if (h == HANDLE_INVALID) return false;

    Slot* s = handle_to_slot(h);
    if (!s) return false;

    /* Compare cached tag - NO mix64 computation! */
#if HANDLE_USE_TOP_BYTE
    if (handle_get_tag(h) != s->tag8) return false;
#else
    if ((handle_get_tag(h) & HANDLE_TAG_MASK) != (s->tag8 & HANDLE_TAG_MASK)) return false;
#endif

    /* Also check flags for extra safety */
    if (s->flags != SLOT_IN_USE) return false;

    return true;
}

/*
 * Get payload from handle with validation.
 *
 * @param h  The handle
 * @return   Payload pointer, or NULL if handle is invalid
 */
static inline void* handle_get_payload(Handle h) {
    if (!handle_is_valid(h)) return NULL;
    return slot_payload(handle_to_slot(h));
}

/*
 * Get pool statistics.
 */
typedef struct SlotPoolStats {
    size_t total_slots;      /* Total slots across all blocks */
    size_t free_slots;       /* Currently free slots */
    size_t in_use_slots;     /* Currently in use */
    size_t peak_in_use;      /* Peak in-use count */
    size_t total_allocs;     /* Lifetime allocation count */
    size_t total_frees;      /* Lifetime free count */
    size_t block_count;      /* Number of blocks */
    size_t memory_bytes;     /* Total memory used by pool */
} SlotPoolStats;

void slot_pool_get_stats(SlotPool* pool, SlotPoolStats* stats);

/* ============== Global Slot Pool ============== */

/*
 * Get the global slot pool for Obj allocations.
 * Lazily initialized on first use.
 */
SlotPool* slot_pool_global(void);

/*
 * Initialize global slot pool explicitly.
 * Call at program start for deterministic initialization.
 */
void slot_pool_global_init(void);

/*
 * Destroy global slot pool.
 * Call at program exit for clean shutdown.
 */
void slot_pool_global_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* PURPLE_SLOT_POOL_H */
