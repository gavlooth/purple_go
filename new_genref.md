```markdown
# Portable Tagged-Pointer Generational Handles (C)

## Problem Statement

We want:
- **1-word handles** (same size as a pointer)
- **Fast validation** (no fat pointer like `{ptr, generation}`)
- **Detection of stale handles** (ABA / use-after-free)
- **Some resistance to guessing/forging**
- **Portable C implementation** (works across architectures)

The challenge:
- Pointer tagging rules differ by architecture
- Only a few bits may be safely stolen from pointers
- Few tag bits alone cause generation collisions

---

## Core Design (Important)

> **The pointer tag is NOT the generation.**

Instead:
- The **real generation counter** lives in memory next to the object
- The pointer only stores a **small tag**
- The tag is derived from:
  - slot identity (pointer)
  - slot generation (from memory)
  - a secret (random at startup)

This completely avoids generational collision problems.

---

## Slot Layout (in memory)

Each allocation lives in a *slot* with a small header:

```c
typedef struct Slot {
    uint32_t generation;   // authoritative generation
    uint32_t flags;        // used/free/etc
    // object payload follows
} Slot;
```

---

## Handle Representation

The handle is a single machine word:

```
[tag bits][ real pointer bits ]
```

---

## Tag Computation (Fast + Keyed)

```c
static inline uint64_t mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static inline uint8_t compute_tag(void* ptr,
                                  uint32_t generation,
                                  uint64_t secret)
{
    uint64_t x = (uint64_t)(uintptr_t)ptr;
    x ^= (uint64_t)generation * 0x9e3779b97f4a7c15ULL;
    x ^= secret;
    return (uint8_t)(mix64(x) >> 56); // take top byte
}
```

---

## Platform Strategy (Targeting All)

### Rule
> **Never assume pointer bits are free. Always provide a safe fallback.**

---

### AArch64 (Top-Byte Tagging)

```c
#if defined(__aarch64__)
#define HAVE_TOP_BYTE_TAG 1
#else
#define HAVE_TOP_BYTE_TAG 0
#endif

#if HAVE_TOP_BYTE_TAG
#define TAG_SHIFT 56
#define TAG_MASK  ((uintptr_t)0xFF << TAG_SHIFT)
#define PTR_MASK  (~TAG_MASK)

static inline uintptr_t encode_handle(void* p, uint8_t tag) {
    return ((uintptr_t)p & PTR_MASK) | ((uintptr_t)tag << TAG_SHIFT);
}

static inline void* decode_ptr(uintptr_t h) {
    return (void*)(h & PTR_MASK);
}

static inline uint8_t decode_tag(uintptr_t h) {
    return (uint8_t)(h >> TAG_SHIFT);
}
#endif
```

---

### Portable Fallback: Low-Bit Tagging

```c
#define LOW_TAG_BITS 4
#define LOW_TAG_MASK ((uintptr_t)((1u << LOW_TAG_BITS) - 1u))

static inline uintptr_t encode_handle_low(void* p, uint8_t tag) {
    uintptr_t x = (uintptr_t)p;
    return (x & ~LOW_TAG_MASK) | (tag & LOW_TAG_MASK);
}

static inline void* decode_ptr_low(uintptr_t h) {
    return (void*)(h & ~LOW_TAG_MASK);
}

static inline uint8_t decode_tag_low(uintptr_t h) {
    return (uint8_t)(h & LOW_TAG_MASK);
}
```

---

## Unified API

```c
typedef uintptr_t handle_t;

static inline handle_t make_handle(Slot* s, uint64_t secret) {
    uint8_t tag = compute_tag(s, s->generation, secret);

#if HAVE_TOP_BYTE_TAG
    return encode_handle(s, tag);
#else
    return encode_handle_low(s, tag);
#endif
}

static inline Slot* handle_ptr(handle_t h) {
#if HAVE_TOP_BYTE_TAG
    return (Slot*)decode_ptr(h);
#else
    return (Slot*)decode_ptr_low(h);
#endif
}

static inline bool validate_handle(handle_t h, uint64_t secret) {
    Slot* s = handle_ptr(h);
    uint32_t gen = s->generation;
    uint8_t expected = compute_tag(s, gen, secret);

#if HAVE_TOP_BYTE_TAG
    return decode_tag(h) == expected;
#else
    return decode_tag_low(h) == (expected & LOW_TAG_MASK);
#endif
}
```

---

## Allocation / Free Rules

```c
static inline void slot_on_allocate(Slot* s) {
    s->generation++;
    if (s->generation == 0) s->generation = 1;
    s->flags = 1;
}

static inline void slot_on_free(Slot* s) {
    s->generation++;
    if (s->generation == 0) s->generation = 1;
    s->flags = 0;
}
```

---

## Key Takeaway

Pointer tagging is **not portable by itself**.  
Portability comes from:
- storing real generations in memory
- using tags only as verifiers
- selecting safe encodings per platform
```
