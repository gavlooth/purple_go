package memory

import (
	"fmt"
	"sync/atomic"
)

// Region References - Vale/Ada/SPARK-style scope hierarchy validation
//
// Key invariant: A pointer cannot point to an object in a more deeply scoped region.
// This prevents dangling references when inner scopes exit.
//
// Region hierarchy:
//   Region A (depth 0)
//     └── Region B (depth 1)
//           └── Region C (depth 2)
//
// Allowed: C → B, C → A, B → A (inner can point to outer)
// Forbidden: A → B, A → C, B → C (outer cannot point to inner)

// RegionID uniquely identifies a region
type RegionID uint64

// Global region ID counter
var nextRegionID uint64 = 1

// RegionDepth represents nesting level (0 = outermost)
type RegionDepth uint32

// Region represents an isolated memory region with scope hierarchy
type Region struct {
	ID       RegionID
	Depth    RegionDepth
	Parent   *Region
	Children []*Region
	Objects  []*RegionObj
	Closed   bool
}

// RegionObj is an object allocated within a region
type RegionObj struct {
	Region *Region
	Data   interface{}
	Refs   []*RegionRef // Outgoing references
}

// RegionRef is a reference that carries region information
type RegionRef struct {
	Target       *RegionObj
	SourceRegion *Region // Region of the object holding this ref
}

// RegionContext manages the region hierarchy
type RegionContext struct {
	Root    *Region
	Current *Region
	Regions map[RegionID]*Region
}

// NewRegionContext creates a new region context with root region
func NewRegionContext() *RegionContext {
	root := &Region{
		ID:       RegionID(atomic.AddUint64(&nextRegionID, 1)),
		Depth:    0,
		Parent:   nil,
		Children: make([]*Region, 0),
		Objects:  make([]*RegionObj, 0),
		Closed:   false,
	}
	ctx := &RegionContext{
		Root:    root,
		Current: root,
		Regions: make(map[RegionID]*Region),
	}
	ctx.Regions[root.ID] = root
	return ctx
}

// EnterRegion creates a new child region and enters it
func (ctx *RegionContext) EnterRegion() *Region {
	region := &Region{
		ID:       RegionID(atomic.AddUint64(&nextRegionID, 1)),
		Depth:    ctx.Current.Depth + 1,
		Parent:   ctx.Current,
		Children: make([]*Region, 0),
		Objects:  make([]*RegionObj, 0),
		Closed:   false,
	}
	ctx.Current.Children = append(ctx.Current.Children, region)
	ctx.Regions[region.ID] = region
	ctx.Current = region
	return region
}

// ExitRegion closes current region and returns to parent
// All objects in the region become invalid
func (ctx *RegionContext) ExitRegion() error {
	if ctx.Current == ctx.Root {
		return fmt.Errorf("cannot exit root region")
	}
	if ctx.Current.Closed {
		return fmt.Errorf("region already closed")
	}

	// Mark region as closed - all objects are now invalid
	ctx.Current.Closed = true

	// Invalidate all objects (they can no longer be dereferenced)
	for _, obj := range ctx.Current.Objects {
		obj.Region = nil // Mark as invalid
	}

	// Return to parent
	ctx.Current = ctx.Current.Parent
	return nil
}

// Alloc allocates an object in the current region
func (ctx *RegionContext) Alloc(data interface{}) *RegionObj {
	obj := &RegionObj{
		Region: ctx.Current,
		Data:   data,
		Refs:   make([]*RegionRef, 0),
	}
	ctx.Current.Objects = append(ctx.Current.Objects, obj)
	return obj
}

// CreateRef creates a reference from source object to target object
// Returns error if this would violate scope hierarchy
func (ctx *RegionContext) CreateRef(source, target *RegionObj) (*RegionRef, error) {
	if source.Region == nil {
		return nil, fmt.Errorf("source object is in closed region")
	}
	if target.Region == nil {
		return nil, fmt.Errorf("target object is in closed region")
	}

	// Key check: source cannot point to more deeply scoped target
	// Allowed: inner → outer (source.Depth >= target.Depth)
	// Forbidden: outer → inner (source.Depth < target.Depth)
	if source.Region.Depth < target.Region.Depth {
		return nil, fmt.Errorf(
			"region violation: cannot create reference from region depth %d to depth %d (outer cannot point to inner)",
			source.Region.Depth, target.Region.Depth,
		)
	}

	ref := &RegionRef{
		Target:       target,
		SourceRegion: source.Region,
	}
	source.Refs = append(source.Refs, ref)
	return ref, nil
}

// Deref safely dereferences a region reference
// Returns error if target region has been closed
func (ref *RegionRef) Deref() (interface{}, error) {
	if ref.Target == nil {
		return nil, fmt.Errorf("null reference")
	}
	if ref.Target.Region == nil {
		return nil, fmt.Errorf("use-after-free: target region has been closed")
	}
	return ref.Target.Data, nil
}

// IsValid checks if a reference is still valid
func (ref *RegionRef) IsValid() bool {
	return ref.Target != nil && ref.Target.Region != nil
}

// CanReference checks if source can safely reference target
// without creating the reference
func CanReference(source, target *RegionObj) bool {
	if source.Region == nil || target.Region == nil {
		return false
	}
	return source.Region.Depth >= target.Region.Depth
}

// IsAncestorRegion checks if ancestor is an ancestor of descendant
func IsAncestorRegion(ancestor, descendant *Region) bool {
	current := descendant
	for current != nil {
		if current == ancestor {
			return true
		}
		current = current.Parent
	}
	return false
}

// GetRegionDepth returns the depth of a region
func (r *Region) GetDepth() RegionDepth {
	return r.Depth
}

// GetObjectCount returns number of objects in region
func (r *Region) GetObjectCount() int {
	return len(r.Objects)
}

// IsClosed returns whether region has been closed
func (r *Region) IsClosed() bool {
	return r.Closed
}
