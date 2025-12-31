package memory

import (
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"sync"
)

// Random Generational References - Vale-style use-after-free detection
//
// Each object has a random 64-bit generation number.
// Each pointer remembers the generation it was created with.
// On dereference: if ptr.gen != obj.gen → use-after-free detected
// On free: obj.gen = 0 (invalidates all existing pointers)
//
// Random vs Sequential:
// - Sequential: obj.gen++ on each reuse (requires tracking, overflow handling)
// - Random: obj.gen = random64() on create, obj.gen = 0 on free (simpler, faster)
//
// Collision probability: 1/2^64 per check (negligible)

// Generation is a 64-bit random generation number
type Generation uint64

// GenObj is an object with generational safety
type GenObj struct {
	Generation Generation
	Data       interface{}
	Freed      bool
	mu         sync.RWMutex // For thread-safe generation checks
}

// GenRef is a generational reference (fat pointer)
type GenRef struct {
	Target             *GenObj
	RememberedGen      Generation
	SourceDescription  string // For debugging: where was this ref created?
}

// GenRefContext manages generational objects
type GenRefContext struct {
	Objects []*GenObj
	mu      sync.Mutex
}

// randomGeneration generates a cryptographically random 64-bit generation
func randomGeneration() Generation {
	var buf [8]byte
	_, err := rand.Read(buf[:])
	if err != nil {
		// Fallback to less random but still usable
		return Generation(0xDEADBEEF)
	}
	return Generation(binary.LittleEndian.Uint64(buf[:]))
}

// NewGenRefContext creates a new generational reference context
func NewGenRefContext() *GenRefContext {
	return &GenRefContext{
		Objects: make([]*GenObj, 0),
	}
}

// Alloc allocates a new object with a random generation
func (ctx *GenRefContext) Alloc(data interface{}) *GenObj {
	obj := &GenObj{
		Generation: randomGeneration(),
		Data:       data,
		Freed:      false,
	}
	ctx.mu.Lock()
	ctx.Objects = append(ctx.Objects, obj)
	ctx.mu.Unlock()
	return obj
}

// CreateRef creates a generational reference to an object
func (obj *GenObj) CreateRef(sourceDesc string) (*GenRef, error) {
	obj.mu.RLock()
	defer obj.mu.RUnlock()

	if obj.Freed {
		return nil, fmt.Errorf("cannot create reference to freed object")
	}
	if obj.Generation == 0 {
		return nil, fmt.Errorf("cannot create reference to invalidated object")
	}

	return &GenRef{
		Target:            obj,
		RememberedGen:     obj.Generation,
		SourceDescription: sourceDesc,
	}, nil
}

// Free marks an object as freed by zeroing its generation
// This invalidates all existing references
func (obj *GenObj) Free() {
	obj.mu.Lock()
	defer obj.mu.Unlock()

	obj.Generation = 0 // Invalidate all pointers
	obj.Freed = true
	obj.Data = nil
}

// Deref safely dereferences a generational reference
// Returns error if generation mismatch (use-after-free)
func (ref *GenRef) Deref() (interface{}, error) {
	if ref.Target == nil {
		return nil, fmt.Errorf("null generational reference")
	}

	ref.Target.mu.RLock()
	defer ref.Target.mu.RUnlock()

	// The key check: remembered generation must match current generation
	if ref.RememberedGen != ref.Target.Generation {
		if ref.Target.Generation == 0 {
			return nil, fmt.Errorf(
				"use-after-free detected: object was freed (gen 0), ref remembered gen %d [created at: %s]",
				ref.RememberedGen, ref.SourceDescription,
			)
		}
		return nil, fmt.Errorf(
			"use-after-free detected: generation mismatch (obj: %d, ref: %d) [created at: %s]",
			ref.Target.Generation, ref.RememberedGen, ref.SourceDescription,
		)
	}

	return ref.Target.Data, nil
}

// IsValid checks if a generational reference is still valid (O(1))
func (ref *GenRef) IsValid() bool {
	if ref.Target == nil {
		return false
	}
	ref.Target.mu.RLock()
	defer ref.Target.mu.RUnlock()
	return ref.RememberedGen == ref.Target.Generation && ref.Target.Generation != 0
}

// MustDeref dereferences or panics (for cases where validity is guaranteed)
func (ref *GenRef) MustDeref() interface{} {
	data, err := ref.Deref()
	if err != nil {
		panic(err)
	}
	return data
}

// GenClosure wraps a closure that captures generational references
type GenClosure struct {
	Captures []*GenRef
	Fn       func() interface{}
}

// NewGenClosure creates a closure with tracked captures
func NewGenClosure(captures []*GenRef, fn func() interface{}) *GenClosure {
	return &GenClosure{
		Captures: captures,
		Fn:       fn,
	}
}

// Call executes the closure after validating all captures
func (c *GenClosure) Call() (interface{}, error) {
	// Validate all captures before executing
	for i, cap := range c.Captures {
		if !cap.IsValid() {
			return nil, fmt.Errorf(
				"closure capture %d is invalid (use-after-free) [created at: %s]",
				i, cap.SourceDescription,
			)
		}
	}
	return c.Fn(), nil
}

// ValidateCaptures checks all captures without executing
func (c *GenClosure) ValidateCaptures() error {
	for i, cap := range c.Captures {
		if !cap.IsValid() {
			return fmt.Errorf(
				"closure capture %d is invalid [created at: %s]",
				i, cap.SourceDescription,
			)
		}
	}
	return nil
}

// Statistics for monitoring
type GenRefStats struct {
	TotalAllocations   int64
	TotalFrees         int64
	TotalDerefs        int64
	UAFDetected        int64 // Use-after-free detections
}
