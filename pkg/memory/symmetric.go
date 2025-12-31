package memory

// Symmetric Reference Counting
//
// Key insight: Treat scope/stack frame as an object that participates in
// the ownership graph. Each reference is bidirectional - both ends know
// about the relationship.
//
// This allows O(1) deterministic cycle collection without global GC:
// - External refs: From live scopes/roots
// - Internal refs: Within the object graph
// - When external_rc drops to 0, the object (or cycle) is orphaned garbage
//
// Reference: Based on symmetric RC research and scope-as-object patterns

// RefType distinguishes external (from scopes) vs internal (from objects) references
type RefType int

const (
	RefExternal RefType = iota // Reference from a scope/root
	RefInternal                // Reference from another object
)

// SymmetricObj represents an object with symmetric reference counting
type SymmetricObj struct {
	ExternalRC int           // References from live scopes
	InternalRC int           // References from other objects
	Refs       []*SymmetricObj // Objects this object references (for cascade)
	Data       interface{}   // Actual data payload
	Freed      bool          // Mark to prevent double-free
}

// SymmetricScope represents a scope that owns objects
type SymmetricScope struct {
	Owned  []*SymmetricObj // Objects owned by this scope
	Parent *SymmetricScope // Parent scope (for nested scopes)
}

// NewSymmetricObj creates a new symmetric object (not yet owned)
func NewSymmetricObj(data interface{}) *SymmetricObj {
	return &SymmetricObj{
		ExternalRC: 0,
		InternalRC: 0,
		Refs:       nil,
		Data:       data,
		Freed:      false,
	}
}

// NewSymmetricScope creates a new scope
func NewSymmetricScope(parent *SymmetricScope) *SymmetricScope {
	return &SymmetricScope{
		Owned:  nil,
		Parent: parent,
	}
}

// ScopeOwn makes a scope own an object (external reference)
func (s *SymmetricScope) Own(obj *SymmetricObj) {
	if obj == nil || obj.Freed {
		return
	}
	obj.ExternalRC++
	s.Owned = append(s.Owned, obj)
}

// ScopeRelease releases all objects owned by this scope
func (s *SymmetricScope) Release() {
	for _, obj := range s.Owned {
		SymmetricDecExternal(obj)
	}
	s.Owned = nil
}

// SymmetricIncRef increments reference count (internal ref from one object to another)
func SymmetricIncRef(from, to *SymmetricObj) {
	if to == nil || to.Freed {
		return
	}
	to.InternalRC++
	if from != nil {
		from.Refs = append(from.Refs, to)
	}
}

// SymmetricDecExternal decrements external reference count
func SymmetricDecExternal(obj *SymmetricObj) {
	if obj == nil || obj.Freed {
		return
	}
	obj.ExternalRC--
	symmetricCheckFree(obj)
}

// SymmetricDecInternal decrements internal reference count
func SymmetricDecInternal(obj *SymmetricObj) {
	if obj == nil || obj.Freed {
		return
	}
	obj.InternalRC--
	symmetricCheckFree(obj)
}

// symmetricCheckFree checks if object should be freed and cascades
func symmetricCheckFree(obj *SymmetricObj) {
	if obj == nil || obj.Freed {
		return
	}

	// Object is garbage when it has no external references
	// (internal refs from other garbage don't count)
	if obj.ExternalRC <= 0 {
		// This object is orphaned - free it and cascade
		obj.Freed = true

		// Decrement internal refs for all objects this one referenced
		for _, ref := range obj.Refs {
			SymmetricDecInternal(ref)
		}
		obj.Refs = nil
		obj.Data = nil
	}
}

// IsOrphaned returns true if the object has no external references
func (obj *SymmetricObj) IsOrphaned() bool {
	return obj.ExternalRC <= 0
}

// TotalRC returns the total reference count (for debugging)
func (obj *SymmetricObj) TotalRC() int {
	return obj.ExternalRC + obj.InternalRC
}

// SymmetricContext manages symmetric RC at a higher level
type SymmetricContext struct {
	GlobalScope *SymmetricScope
	ScopeStack  []*SymmetricScope
	Stats       SymmetricStats
}

// SymmetricStats tracks statistics for symmetric RC
type SymmetricStats struct {
	ObjectsCreated   int
	ObjectsFreed     int
	ExternalIncRefs  int
	ExternalDecRefs  int
	InternalIncRefs  int
	InternalDecRefs  int
	CyclesCollected  int
}

// NewSymmetricContext creates a new symmetric RC context
func NewSymmetricContext() *SymmetricContext {
	global := NewSymmetricScope(nil)
	return &SymmetricContext{
		GlobalScope: global,
		ScopeStack:  []*SymmetricScope{global},
	}
}

// CurrentScope returns the current (innermost) scope
func (ctx *SymmetricContext) CurrentScope() *SymmetricScope {
	if len(ctx.ScopeStack) == 0 {
		return ctx.GlobalScope
	}
	return ctx.ScopeStack[len(ctx.ScopeStack)-1]
}

// EnterScope creates and enters a new scope
func (ctx *SymmetricContext) EnterScope() *SymmetricScope {
	parent := ctx.CurrentScope()
	scope := NewSymmetricScope(parent)
	ctx.ScopeStack = append(ctx.ScopeStack, scope)
	return scope
}

// ExitScope exits the current scope and releases its objects
func (ctx *SymmetricContext) ExitScope() {
	if len(ctx.ScopeStack) <= 1 {
		// Don't exit global scope
		return
	}
	scope := ctx.ScopeStack[len(ctx.ScopeStack)-1]
	ctx.ScopeStack = ctx.ScopeStack[:len(ctx.ScopeStack)-1]

	// Count cycles before release
	cyclesBefore := ctx.countOrphanedCycles(scope)

	scope.Release()

	// Any objects freed that were in cycles count as cycle collection
	ctx.Stats.CyclesCollected += cyclesBefore
}

// Alloc allocates a new object owned by the current scope
func (ctx *SymmetricContext) Alloc(data interface{}) *SymmetricObj {
	obj := NewSymmetricObj(data)
	ctx.CurrentScope().Own(obj)
	ctx.Stats.ObjectsCreated++
	ctx.Stats.ExternalIncRefs++
	return obj
}

// Link creates an internal reference from one object to another
func (ctx *SymmetricContext) Link(from, to *SymmetricObj) {
	SymmetricIncRef(from, to)
	ctx.Stats.InternalIncRefs++
}

// countOrphanedCycles counts objects that would be orphaned cycles
func (ctx *SymmetricContext) countOrphanedCycles(scope *SymmetricScope) int {
	count := 0
	for _, obj := range scope.Owned {
		// Object with internal refs but will lose external ref = part of cycle
		if obj.InternalRC > 0 && obj.ExternalRC == 1 {
			count++
		}
	}
	return count
}

// GetStats returns current statistics
func (ctx *SymmetricContext) GetStats() SymmetricStats {
	return ctx.Stats
}
