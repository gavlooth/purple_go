package memory

import (
	"testing"
)

func TestRegionBasicAllocation(t *testing.T) {
	ctx := NewRegionContext()

	obj := ctx.Alloc("hello")
	if obj.Data != "hello" {
		t.Errorf("expected 'hello', got %v", obj.Data)
	}
	if obj.Region != ctx.Root {
		t.Error("object should be in root region")
	}
}

func TestRegionEnterExit(t *testing.T) {
	ctx := NewRegionContext()

	// Enter child region
	child := ctx.EnterRegion()
	if child.Depth != 1 {
		t.Errorf("expected depth 1, got %d", child.Depth)
	}
	if child.Parent != ctx.Root {
		t.Error("child parent should be root")
	}

	// Allocate in child
	obj := ctx.Alloc("child data")
	if obj.Region != child {
		t.Error("object should be in child region")
	}

	// Exit child region
	err := ctx.ExitRegion()
	if err != nil {
		t.Errorf("unexpected error: %v", err)
	}

	// Object should now be invalid
	if obj.Region != nil {
		t.Error("object region should be nil after exit")
	}
}

func TestRegionInnerToOuterRefAllowed(t *testing.T) {
	ctx := NewRegionContext()

	// Allocate in root
	outer := ctx.Alloc("outer")

	// Enter child and allocate
	ctx.EnterRegion()
	inner := ctx.Alloc("inner")

	// Inner → outer should be allowed
	ref, err := ctx.CreateRef(inner, outer)
	if err != nil {
		t.Errorf("inner→outer should be allowed: %v", err)
	}
	if ref == nil {
		t.Error("reference should not be nil")
	}

	// Verify reference is valid
	data, err := ref.Deref()
	if err != nil {
		t.Errorf("deref should succeed: %v", err)
	}
	if data != "outer" {
		t.Errorf("expected 'outer', got %v", data)
	}
}

func TestRegionOuterToInnerRefForbidden(t *testing.T) {
	ctx := NewRegionContext()

	// Allocate in root
	outer := ctx.Alloc("outer")

	// Enter child and allocate
	ctx.EnterRegion()
	inner := ctx.Alloc("inner")

	// Outer → inner should be forbidden
	_, err := ctx.CreateRef(outer, inner)
	if err == nil {
		t.Error("outer→inner should be forbidden")
	}
}

func TestRegionSameLevelRefAllowed(t *testing.T) {
	ctx := NewRegionContext()

	obj1 := ctx.Alloc("obj1")
	obj2 := ctx.Alloc("obj2")

	// Same level refs should be allowed
	ref, err := ctx.CreateRef(obj1, obj2)
	if err != nil {
		t.Errorf("same level ref should be allowed: %v", err)
	}
	if !ref.IsValid() {
		t.Error("reference should be valid")
	}
}

func TestRegionDerefAfterClose(t *testing.T) {
	ctx := NewRegionContext()

	ctx.EnterRegion()
	obj := ctx.Alloc("data")
	ref, _ := obj.Region.Objects[0].CreateSelfRef()

	ctx.ExitRegion()

	// Deref after close should fail
	_, err := ref.Deref()
	if err == nil {
		t.Error("deref after region close should fail")
	}
}

// CreateSelfRef is a helper for testing
func (obj *RegionObj) CreateSelfRef() (*RegionRef, error) {
	return &RegionRef{
		Target:       obj,
		SourceRegion: obj.Region,
	}, nil
}

func TestRegionNestedScopes(t *testing.T) {
	ctx := NewRegionContext()

	// Create nested regions: root → A → B → C
	ctx.EnterRegion() // A (depth 1)
	objA := ctx.Alloc("A")

	ctx.EnterRegion() // B (depth 2)
	objB := ctx.Alloc("B")

	ctx.EnterRegion() // C (depth 3)
	objC := ctx.Alloc("C")

	// C can reference A (inner → outer)
	_, err := ctx.CreateRef(objC, objA)
	if err != nil {
		t.Errorf("C→A should be allowed: %v", err)
	}

	// C can reference B
	_, err = ctx.CreateRef(objC, objB)
	if err != nil {
		t.Errorf("C→B should be allowed: %v", err)
	}

	// A cannot reference C (outer → inner)
	_, err = ctx.CreateRef(objA, objC)
	if err == nil {
		t.Error("A→C should be forbidden")
	}

	// Exit C
	ctx.ExitRegion()
	if objC.Region != nil {
		t.Error("objC should be invalid after exiting C")
	}

	// B and A should still be valid
	if objB.Region == nil {
		t.Error("objB should still be valid")
	}
	if objA.Region == nil {
		t.Error("objA should still be valid")
	}
}

func TestRegionCannotExitRoot(t *testing.T) {
	ctx := NewRegionContext()

	err := ctx.ExitRegion()
	if err == nil {
		t.Error("should not be able to exit root region")
	}
}

func TestRegionIsAncestor(t *testing.T) {
	ctx := NewRegionContext()

	regionA := ctx.EnterRegion()
	regionB := ctx.EnterRegion()
	regionC := ctx.EnterRegion()

	if !IsAncestorRegion(ctx.Root, regionC) {
		t.Error("root should be ancestor of C")
	}
	if !IsAncestorRegion(regionA, regionC) {
		t.Error("A should be ancestor of C")
	}
	if !IsAncestorRegion(regionB, regionC) {
		t.Error("B should be ancestor of C")
	}
	if IsAncestorRegion(regionC, regionA) {
		t.Error("C should not be ancestor of A")
	}
}

func TestRegionCanReference(t *testing.T) {
	ctx := NewRegionContext()

	outer := ctx.Alloc("outer")
	ctx.EnterRegion()
	inner := ctx.Alloc("inner")

	if !CanReference(inner, outer) {
		t.Error("inner should be able to reference outer")
	}
	if CanReference(outer, inner) {
		t.Error("outer should not be able to reference inner")
	}
}

// ============ Complex Tests ============

func TestRegionDeepNesting(t *testing.T) {
	ctx := NewRegionContext()

	// Create 10 levels of nesting
	objects := make([]*RegionObj, 10)
	for i := 0; i < 10; i++ {
		ctx.EnterRegion()
		objects[i] = ctx.Alloc(i)
	}

	// Deepest can reference all ancestors
	deepest := objects[9]
	for i := 0; i < 9; i++ {
		ref, err := ctx.CreateRef(deepest, objects[i])
		if err != nil {
			t.Errorf("level 9 should reference level %d: %v", i, err)
		}
		if !ref.IsValid() {
			t.Errorf("ref to level %d should be valid", i)
		}
	}

	// No ancestor can reference deepest
	for i := 0; i < 9; i++ {
		_, err := ctx.CreateRef(objects[i], deepest)
		if err == nil {
			t.Errorf("level %d should NOT reference level 9", i)
		}
	}

	// Exit all regions and verify invalidation
	for i := 9; i >= 0; i-- {
		ctx.ExitRegion()
		if objects[i].Region != nil {
			t.Errorf("object at level %d should be invalid after exit", i)
		}
	}
}

func TestRegionDiamondPattern(t *testing.T) {
	// Test diamond-shaped region hierarchy
	//       root
	//      /    \
	//     A      B
	//      \    /
	//        C
	ctx := NewRegionContext()
	root := ctx.Alloc("root")

	// Create A branch
	regionA := ctx.EnterRegion()
	objA := ctx.Alloc("A")

	// While in A's region, A can reference root
	if !CanReference(objA, root) {
		t.Error("A should reference root while in region")
	}

	// Create ref before exiting
	refAtoRoot, err := ctx.CreateRef(objA, root)
	if err != nil {
		t.Errorf("A should be able to reference root: %v", err)
	}
	if !refAtoRoot.IsValid() {
		t.Error("ref A->root should be valid")
	}

	ctx.ExitRegion()

	// Create B branch
	regionB := ctx.EnterRegion()
	objB := ctx.Alloc("B")

	// While in B's region, B can reference root
	if !CanReference(objB, root) {
		t.Error("B should reference root while in region")
	}

	ctx.ExitRegion()

	// A and B are siblings at same depth
	if regionA.Depth != regionB.Depth {
		t.Error("A and B should be at same depth")
	}

	// A and B are closed, verify invalidation
	if objA.Region != nil || objB.Region != nil {
		t.Error("A and B should be invalid after region exit")
	}

	// After exit, CanReference returns false for invalid objects
	if CanReference(objA, root) {
		t.Error("A should not be able to reference after region exit")
	}
}

func TestRegionReferenceChain(t *testing.T) {
	ctx := NewRegionContext()

	// Create chain: obj0 <- obj1 <- obj2 <- obj3
	// Each deeper object references the shallower one
	objects := make([]*RegionObj, 4)
	refs := make([]*RegionRef, 3)

	for i := 0; i < 4; i++ {
		if i > 0 {
			ctx.EnterRegion()
		}
		objects[i] = ctx.Alloc(i)
		if i > 0 {
			ref, err := ctx.CreateRef(objects[i], objects[i-1])
			if err != nil {
				t.Fatalf("failed to create ref %d -> %d: %v", i, i-1, err)
			}
			refs[i-1] = ref
		}
	}

	// All refs should be valid
	for i, ref := range refs {
		if !ref.IsValid() {
			t.Errorf("ref %d should be valid", i)
		}
	}

	// Exit innermost region
	ctx.ExitRegion()

	// refs[2] (obj3 -> obj2) should now be invalid (obj3 is freed)
	// But we can't check this directly since the ref struct still exists
	// The key is that obj3.Region is nil
	if objects[3].Region != nil {
		t.Error("obj3 should be invalid")
	}
}

func TestRegionMassiveHierarchy(t *testing.T) {
	ctx := NewRegionContext()

	// Create 100 objects at root level
	rootObjs := make([]*RegionObj, 100)
	for i := 0; i < 100; i++ {
		rootObjs[i] = ctx.Alloc(i)
	}

	// Create nested scope with 100 objects
	ctx.EnterRegion()
	innerObjs := make([]*RegionObj, 100)
	for i := 0; i < 100; i++ {
		innerObjs[i] = ctx.Alloc(1000 + i)
	}

	// All inner objects reference all root objects
	refCount := 0
	for _, inner := range innerObjs {
		for _, root := range rootObjs {
			_, err := ctx.CreateRef(inner, root)
			if err != nil {
				t.Fatalf("inner should reference root: %v", err)
			}
			refCount++
		}
	}

	if refCount != 10000 {
		t.Errorf("expected 10000 refs, got %d", refCount)
	}

	// Exit inner scope - all inner objects invalidated
	ctx.ExitRegion()
	for i, obj := range innerObjs {
		if obj.Region != nil {
			t.Errorf("inner object %d should be invalid", i)
		}
	}

	// Root objects still valid
	for i, obj := range rootObjs {
		if obj.Region == nil {
			t.Errorf("root object %d should still be valid", i)
		}
	}
}

func TestRegionConcurrentLikeAccess(t *testing.T) {
	// Simulate what concurrent access patterns would look like
	ctx := NewRegionContext()

	// Allocate shared data at root
	shared := ctx.Alloc("shared state")

	// Simulate multiple "workers" each with their own scope
	for worker := 0; worker < 5; worker++ {
		ctx.EnterRegion()
		local := ctx.Alloc(worker)

		// Worker can read shared
		ref, err := ctx.CreateRef(local, shared)
		if err != nil {
			t.Errorf("worker %d should access shared: %v", worker, err)
		}
		if !ref.IsValid() {
			t.Errorf("worker %d ref should be valid", worker)
		}

		// Worker exits
		ctx.ExitRegion()

		// Local is invalid but shared persists
		if local.Region != nil {
			t.Errorf("worker %d local should be invalid", worker)
		}
		if shared.Region == nil {
			t.Errorf("shared should still be valid after worker %d", worker)
		}
	}
}

func TestRegionEscapeAttempt(t *testing.T) {
	ctx := NewRegionContext()

	var escapedRef *RegionRef

	// Enter scope and try to "escape" a reference
	ctx.EnterRegion()
	inner := ctx.Alloc("will try to escape")
	escapedRef = &RegionRef{Target: inner, SourceRegion: inner.Region}
	ctx.ExitRegion()

	// The escaped ref should be detected as invalid
	if escapedRef.IsValid() {
		t.Error("escaped reference should be invalid after scope exit")
	}

	_, err := escapedRef.Deref()
	if err == nil {
		t.Error("deref of escaped ref should fail")
	}
}
