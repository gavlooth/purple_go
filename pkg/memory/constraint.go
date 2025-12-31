package memory

import (
	"fmt"
	"sync"
	"sync/atomic"
)

// Constraint References - Assertion-based safety for complex patterns
//
// For graphs, observers, callbacks, and other complex ownership patterns.
//
// Key concept:
// - Each object has ONE owner (responsible for freeing)
// - Multiple non-owning "constraint" references can exist
// - On free: ASSERT that constraint count is zero
// - If assertion fails: dangling references would have been created
//
// This is primarily a DEBUG/DEVELOPMENT tool - catches errors at free time
// rather than at dereference time.

// ConstraintObj is an object with constraint reference tracking
type ConstraintObj struct {
	Data            interface{}
	Owner           string // Description of owner for debugging
	ConstraintCount int32  // Number of active constraint references (atomic)
	Freed           bool   // Whether the object has been freed

	// Debug info - only used when debug tracking is enabled
	mu                sync.Mutex
	ConstraintSources map[uint64]string // refID -> source (only in debug mode)
	nextRefID         uint64
	debugMode         bool
}

// ConstraintRef is a non-owning reference that constrains object lifetime
type ConstraintRef struct {
	Target   *ConstraintObj
	Source   string // Where this ref was created
	refID    uint64 // Unique ID for O(1) removal
	released int32  // Atomic flag
}

// ConstraintContext manages constraint objects
type ConstraintContext struct {
	Objects       []*ConstraintObj
	AssertOnError bool     // If true, panic on constraint violation
	Violations    []string // Recorded violations if not asserting
	DebugMode     bool     // Enable source tracking (slower but more info)
	mu            sync.Mutex
}

// NewConstraintContext creates a new constraint context
func NewConstraintContext(assertOnError bool) *ConstraintContext {
	return &ConstraintContext{
		Objects:       make([]*ConstraintObj, 0),
		AssertOnError: assertOnError,
		Violations:    make([]string, 0),
		DebugMode:     false, // Off by default for performance
	}
}

// NewConstraintContextDebug creates a context with debug tracking enabled
func NewConstraintContextDebug(assertOnError bool) *ConstraintContext {
	return &ConstraintContext{
		Objects:       make([]*ConstraintObj, 0),
		AssertOnError: assertOnError,
		Violations:    make([]string, 0),
		DebugMode:     true,
	}
}

// Alloc allocates a new constraint object with an owner
func (ctx *ConstraintContext) Alloc(data interface{}, owner string) *ConstraintObj {
	obj := &ConstraintObj{
		Data:      data,
		Owner:     owner,
		debugMode: ctx.DebugMode,
	}
	if ctx.DebugMode {
		obj.ConstraintSources = make(map[uint64]string)
	}
	ctx.mu.Lock()
	ctx.Objects = append(ctx.Objects, obj)
	ctx.mu.Unlock()
	return obj
}

// AddConstraint creates a constraint reference to an object
// The constraint must be released before the object can be freed
func (obj *ConstraintObj) AddConstraint(source string) (*ConstraintRef, error) {
	obj.mu.Lock()
	if obj.Freed {
		obj.mu.Unlock()
		return nil, fmt.Errorf("cannot add constraint to freed object")
	}

	// Increment constraint count
	atomic.AddInt32(&obj.ConstraintCount, 1)

	ref := &ConstraintRef{
		Target: obj,
		Source: source,
	}

	// Only track sources in debug mode
	if obj.debugMode {
		obj.nextRefID++
		ref.refID = obj.nextRefID
		obj.ConstraintSources[ref.refID] = source
	}
	obj.mu.Unlock()

	return ref, nil
}

// Release releases a constraint reference
func (ref *ConstraintRef) Release() error {
	// Atomic check-and-set for released flag
	if !atomic.CompareAndSwapInt32(&ref.released, 0, 1) {
		return fmt.Errorf("constraint already released [%s]", ref.Source)
	}
	if ref.Target == nil {
		return fmt.Errorf("null constraint reference")
	}

	// Atomically decrement constraint count
	newCount := atomic.AddInt32(&ref.Target.ConstraintCount, -1)
	if newCount < 0 {
		// Restore and return error
		atomic.AddInt32(&ref.Target.ConstraintCount, 1)
		atomic.StoreInt32(&ref.released, 0)
		return fmt.Errorf("constraint count underflow")
	}

	// Remove from sources map only in debug mode
	if ref.Target.debugMode {
		ref.Target.mu.Lock()
		delete(ref.Target.ConstraintSources, ref.refID)
		ref.Target.mu.Unlock()
	}

	return nil
}

// Free attempts to free a constraint object
// Returns error if constraints still exist
func (ctx *ConstraintContext) Free(obj *ConstraintObj) error {
	obj.mu.Lock()
	defer obj.mu.Unlock()

	if obj.Freed {
		return fmt.Errorf("double free detected [owner: %s]", obj.Owner)
	}

	count := atomic.LoadInt32(&obj.ConstraintCount)
	if count > 0 {
		var sources []string
		if obj.debugMode {
			sources = make([]string, 0, len(obj.ConstraintSources))
			for _, src := range obj.ConstraintSources {
				sources = append(sources, src)
			}
		}

		violation := fmt.Sprintf(
			"constraint violation: cannot free object [owner: %s] with %d active constraints",
			obj.Owner, count,
		)
		if len(sources) > 0 {
			violation += fmt.Sprintf(" from: %v", sources)
		}

		ctx.mu.Lock()
		ctx.Violations = append(ctx.Violations, violation)
		ctx.mu.Unlock()

		if ctx.AssertOnError {
			panic(violation)
		}
		return fmt.Errorf(violation)
	}

	obj.Freed = true
	obj.Data = nil
	return nil
}

// MustFree frees the object, panicking if constraints exist
func (ctx *ConstraintContext) MustFree(obj *ConstraintObj) {
	err := ctx.Free(obj)
	if err != nil {
		panic(err)
	}
}

// Deref safely dereferences a constraint reference
func (ref *ConstraintRef) Deref() (interface{}, error) {
	if ref.Target == nil {
		return nil, fmt.Errorf("null constraint reference")
	}
	if atomic.LoadInt32(&ref.released) == 1 {
		return nil, fmt.Errorf("dereferencing released constraint [%s]", ref.Source)
	}
	ref.Target.mu.Lock()
	if ref.Target.Freed {
		ref.Target.mu.Unlock()
		return nil, fmt.Errorf("use-after-free: object was freed [owner: %s, ref: %s]",
			ref.Target.Owner, ref.Source)
	}
	data := ref.Target.Data
	ref.Target.mu.Unlock()
	return data, nil
}

// IsValid checks if a constraint reference is valid
func (ref *ConstraintRef) IsValid() bool {
	if ref.Target == nil {
		return false
	}
	if atomic.LoadInt32(&ref.released) == 1 {
		return false
	}
	ref.Target.mu.Lock()
	freed := ref.Target.Freed
	ref.Target.mu.Unlock()
	return !freed
}

// GetViolations returns all recorded violations
func (ctx *ConstraintContext) GetViolations() []string {
	ctx.mu.Lock()
	defer ctx.mu.Unlock()
	result := make([]string, len(ctx.Violations))
	copy(result, ctx.Violations)
	return result
}

// HasViolations checks if any violations occurred
func (ctx *ConstraintContext) HasViolations() bool {
	ctx.mu.Lock()
	defer ctx.mu.Unlock()
	return len(ctx.Violations) > 0
}

// ClearViolations clears the violation log
func (ctx *ConstraintContext) ClearViolations() {
	ctx.mu.Lock()
	defer ctx.mu.Unlock()
	ctx.Violations = ctx.Violations[:0]
}

// ConstraintStats provides statistics
type ConstraintStats struct {
	TotalObjects     int
	ActiveObjects    int
	TotalConstraints int
	ViolationCount   int
}

// GetStats returns current statistics
func (ctx *ConstraintContext) GetStats() ConstraintStats {
	ctx.mu.Lock()
	defer ctx.mu.Unlock()

	active := 0
	totalConstraints := 0
	for _, obj := range ctx.Objects {
		obj.mu.Lock()
		if !obj.Freed {
			active++
			totalConstraints += int(atomic.LoadInt32(&obj.ConstraintCount))
		}
		obj.mu.Unlock()
	}

	return ConstraintStats{
		TotalObjects:     len(ctx.Objects),
		ActiveObjects:    active,
		TotalConstraints: totalConstraints,
		ViolationCount:   len(ctx.Violations),
	}
}
