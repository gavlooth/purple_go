/*
 * Stable Slot Pool Implementation
 *
 * See slot_pool.h for design rationale.
 */

#include "slot_pool.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============== Mixing Function ============== */

/*
 * splitmix64 finalizer - bijective mixing function.
 * Used to compute validation tags from (slot_ptr, generation, secret).
 */
static inline uint64_t mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

/*
 * Compute validation tag for a slot.
 * Called on alloc/free to update the cached tag.
 */
static inline uint8_t compute_tag8(SlotPool* pool, Slot* slot) {
    uint64_t x = (uint64_t)(uintptr_t)slot;
    x ^= (uint64_t)slot->generation * 0x9e3779b97f4a7c15ULL;
    x ^= pool->secret;
    return (uint8_t)(mix64(x) >> 56);
}

/* ============== Block Management ============== */

static SlotBlock* create_block(size_t slot_count, size_t slot_stride) {
    SlotBlock* block = malloc(sizeof(SlotBlock));
    if (!block) return NULL;

    /* Allocate aligned memory for slots */
    size_t total_size = slot_count * slot_stride;

    /* Use aligned_alloc if available, otherwise malloc with manual alignment */
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
    int err = posix_memalign((void**)&block->slots, 16, total_size);
    if (err != 0) {
        free(block);
        return NULL;
    }
#else
    block->slots = malloc(total_size + 15);
    if (!block->slots) {
        free(block);
        return NULL;
    }
    /* Align to 16 bytes */
    block->slots = (Slot*)(((uintptr_t)block->slots + 15) & ~15);
#endif

    memset(block->slots, 0, total_size);
    block->slot_count = slot_count;
    block->slot_stride = slot_stride;
    block->next = NULL;

    return block;
}

static void destroy_block(SlotBlock* block) {
    if (!block) return;
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
    free(block->slots);
#else
    /* Note: if we used manual alignment, we'd need to store original ptr */
    free(block->slots);
#endif
    free(block);
}

static Slot* get_slot_at_index(SlotBlock* block, size_t index) {
    return (Slot*)((char*)block->slots + index * block->slot_stride);
}

/* ============== Pool Creation/Destruction ============== */

SlotPool* slot_pool_create(size_t payload_size, size_t alignment, size_t initial_slots) {
    SlotPool* pool = malloc(sizeof(SlotPool));
    if (!pool) return NULL;

    memset(pool, 0, sizeof(SlotPool));

    /* Compute slot stride (header + payload, aligned) */
    size_t header_size = sizeof(Slot);
    size_t min_stride = header_size + payload_size;
    pool->alignment = alignment < 16 ? 16 : alignment;
    pool->slot_stride = (min_stride + pool->alignment - 1) & ~(pool->alignment - 1);
    pool->payload_size = payload_size;

    /* Initialize secret for tag computation */
    pool->secret = (uint64_t)time(NULL);
    pool->secret ^= (uint64_t)(uintptr_t)pool;
    pool->secret *= 0x5851f42d4c957f2dULL;
    pool->secret ^= (uint64_t)(uintptr_t)&pool->secret;
    pool->secret = mix64(pool->secret);

    /* Create initial block */
    if (initial_slots == 0) initial_slots = SLOT_POOL_BLOCK_SIZE;

    SlotBlock* block = create_block(initial_slots, pool->slot_stride);
    if (!block) {
        free(pool);
        return NULL;
    }
    pool->blocks = block;
    pool->block_count = 1;

    /* Initialize freelist */
    pool->freelist_capacity = initial_slots;
    pool->freelist = malloc(pool->freelist_capacity * sizeof(Slot*));
    if (!pool->freelist) {
        destroy_block(block);
        free(pool);
        return NULL;
    }
    pool->freelist_top = initial_slots;

    /* Initialize all slots and populate freelist */
    for (size_t i = 0; i < initial_slots; i++) {
        Slot* slot = get_slot_at_index(block, i);
        /* Start with random generation for unpredictability */
        uint64_t rand_gen = mix64(pool->secret ^ (i * 0x9e3779b97f4a7c15ULL));
        slot->generation = (uint32_t)rand_gen;
        if (slot->generation == 0) slot->generation = 1;
        slot->flags = SLOT_FREE;
        slot->size_class = 0;
        slot->tag8 = compute_tag8(pool, slot);

        /* Add to freelist (in reverse order so first alloc gets slot 0) */
        pool->freelist[initial_slots - 1 - i] = slot;
    }

    return pool;
}

void slot_pool_destroy(SlotPool* pool) {
    if (!pool) return;

    /* Free all blocks */
    SlotBlock* block = pool->blocks;
    while (block) {
        SlotBlock* next = block->next;
        destroy_block(block);
        block = next;
    }

    free(pool->freelist);
    free(pool);
}

/* ============== Pool Growth ============== */

static bool pool_grow(SlotPool* pool) {
    if (pool->block_count >= SLOT_POOL_MAX_BLOCKS) {
        return false;  /* Hit maximum */
    }

    /* Double the block size each time (geometric growth) */
    size_t new_count = pool->blocks->slot_count;
    if (new_count < SLOT_POOL_BLOCK_SIZE * 4) {
        new_count *= 2;
    }

    SlotBlock* block = create_block(new_count, pool->slot_stride);
    if (!block) return false;

    /* Add to block list */
    block->next = pool->blocks;
    pool->blocks = block;
    pool->block_count++;

    /* Expand freelist */
    size_t new_capacity = pool->freelist_capacity + new_count;
    Slot** new_freelist = realloc(pool->freelist, new_capacity * sizeof(Slot*));
    if (!new_freelist) {
        /* Rollback */
        pool->blocks = block->next;
        pool->block_count--;
        destroy_block(block);
        return false;
    }
    pool->freelist = new_freelist;
    pool->freelist_capacity = new_capacity;

    /* Initialize new slots and add to freelist */
    for (size_t i = 0; i < new_count; i++) {
        Slot* slot = get_slot_at_index(block, i);
        uint64_t rand_gen = mix64(pool->secret ^ ((pool->total_allocs + i) * 0x9e3779b97f4a7c15ULL));
        slot->generation = (uint32_t)rand_gen;
        if (slot->generation == 0) slot->generation = 1;
        slot->flags = SLOT_FREE;
        slot->size_class = 0;
        slot->tag8 = compute_tag8(pool, slot);

        pool->freelist[pool->freelist_top++] = slot;
    }

    return true;
}

/* ============== Allocation/Free ============== */

Slot* slot_pool_alloc(SlotPool* pool) {
    if (!pool) return NULL;

    /* Try freelist first */
    if (pool->freelist_top == 0) {
        /* Need to grow */
        if (!pool_grow(pool)) {
            return NULL;  /* Can't grow */
        }
    }

    /* Pop from freelist */
    Slot* slot = pool->freelist[--pool->freelist_top];

    /* Evolve generation and recompute tag */
    slot->generation++;
    if (slot->generation == 0) slot->generation = 1;  /* Skip 0 */
    slot->tag8 = compute_tag8(pool, slot);
    slot->flags = SLOT_IN_USE;

    /* Statistics */
    pool->total_allocs++;
    pool->current_in_use++;
    if (pool->current_in_use > pool->peak_in_use) {
        pool->peak_in_use = pool->current_in_use;
    }

    return slot;
}

void slot_pool_free(SlotPool* pool, Slot* slot) {
    if (!pool || !slot) return;
    if (slot->flags == SLOT_FREE) return;  /* Already free */

    /* Evolve generation - invalidates existing handles */
    slot->generation++;
    if (slot->generation == 0) slot->generation = 1;
    slot->tag8 = compute_tag8(pool, slot);
    slot->flags = SLOT_FREE;

    /* Push to freelist - slot memory STAYS ALLOCATED */
    pool->freelist[pool->freelist_top++] = slot;

    /* Statistics */
    pool->total_frees++;
    pool->current_in_use--;
}

/* ============== Handle Operations ============== */

Handle slot_pool_make_handle(SlotPool* pool, Slot* slot) {
    (void)pool;  /* Secret already baked into tag8 */
    if (!slot) return HANDLE_INVALID;

#if HANDLE_USE_TOP_BYTE
    return ((uintptr_t)slot & HANDLE_PTR_MASK) |
           ((uintptr_t)slot->tag8 << HANDLE_TAG_SHIFT);
#else
    return ((uintptr_t)slot & HANDLE_PTR_MASK) |
           ((uintptr_t)(slot->tag8 & HANDLE_TAG_MASK));
#endif
}

/* ============== Statistics ============== */

void slot_pool_get_stats(SlotPool* pool, SlotPoolStats* stats) {
    if (!pool || !stats) return;

    memset(stats, 0, sizeof(SlotPoolStats));

    /* Count total slots across blocks */
    size_t total = 0;
    size_t memory = sizeof(SlotPool) + pool->freelist_capacity * sizeof(Slot*);

    for (SlotBlock* b = pool->blocks; b; b = b->next) {
        total += b->slot_count;
        memory += sizeof(SlotBlock) + b->slot_count * b->slot_stride;
    }

    stats->total_slots = total;
    stats->free_slots = pool->freelist_top;
    stats->in_use_slots = pool->current_in_use;
    stats->peak_in_use = pool->peak_in_use;
    stats->total_allocs = pool->total_allocs;
    stats->total_frees = pool->total_frees;
    stats->block_count = pool->block_count;
    stats->memory_bytes = memory;
}

/* ============== Global Slot Pool ============== */

static SlotPool* g_global_slot_pool = NULL;

/* Forward declaration of Obj size - will be defined in runtime.c */
#ifndef PURPLE_OBJ_SIZE
#define PURPLE_OBJ_SIZE 48  /* Default, should match sizeof(Obj) */
#endif

SlotPool* slot_pool_global(void) {
    if (!g_global_slot_pool) {
        slot_pool_global_init();
    }
    return g_global_slot_pool;
}

void slot_pool_global_init(void) {
    if (g_global_slot_pool) return;
    g_global_slot_pool = slot_pool_create(PURPLE_OBJ_SIZE, 16, SLOT_POOL_BLOCK_SIZE);
}

void slot_pool_global_destroy(void) {
    if (g_global_slot_pool) {
        slot_pool_destroy(g_global_slot_pool);
        g_global_slot_pool = NULL;
    }
}
