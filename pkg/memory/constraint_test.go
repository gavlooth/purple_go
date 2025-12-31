package memory

import (
	"strings"
	"testing"
)

func TestConstraintBasicAllocation(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "test owner")
	if obj.Data != "data" {
		t.Errorf("expected 'data', got %v", obj.Data)
	}
	if obj.Owner != "test owner" {
		t.Error("owner mismatch")
	}
	if obj.ConstraintCount != 0 {
		t.Error("initial constraint count should be 0")
	}
}

func TestConstraintAddAndRelease(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")

	// Add constraint
	ref, err := obj.AddConstraint("observer1")
	if err != nil {
		t.Errorf("unexpected error: %v", err)
	}
	if obj.ConstraintCount != 1 {
		t.Errorf("expected constraint count 1, got %d", obj.ConstraintCount)
	}

	// Release constraint
	err = ref.Release()
	if err != nil {
		t.Errorf("release failed: %v", err)
	}
	if obj.ConstraintCount != 0 {
		t.Errorf("expected constraint count 0, got %d", obj.ConstraintCount)
	}
}

func TestConstraintFreeWithNoConstraints(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")

	err := ctx.Free(obj)
	if err != nil {
		t.Errorf("free should succeed with no constraints: %v", err)
	}
	if !obj.Freed {
		t.Error("object should be marked as freed")
	}
}

func TestConstraintFreeWithActiveConstraints(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	obj.AddConstraint("observer1")
	obj.AddConstraint("observer2")

	// Free should fail
	err := ctx.Free(obj)
	if err == nil {
		t.Error("free should fail with active constraints")
	}
	if !strings.Contains(err.Error(), "constraint violation") {
		t.Errorf("error should mention constraint violation: %v", err)
	}

	// Check violation was recorded
	if !ctx.HasViolations() {
		t.Error("violation should be recorded")
	}
}

func TestConstraintFreeAfterAllReleased(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	ref1, _ := obj.AddConstraint("observer1")
	ref2, _ := obj.AddConstraint("observer2")

	ref1.Release()
	ref2.Release()

	// Now free should succeed
	err := ctx.Free(obj)
	if err != nil {
		t.Errorf("free should succeed after all constraints released: %v", err)
	}
}

func TestConstraintPanicOnViolation(t *testing.T) {
	ctx := NewConstraintContext(true) // Assert mode

	obj := ctx.Alloc("data", "owner")
	obj.AddConstraint("observer")

	defer func() {
		if r := recover(); r == nil {
			t.Error("should panic on constraint violation")
		}
	}()

	ctx.Free(obj) // Should panic
}

func TestConstraintDeref(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("test data", "owner")
	ref, _ := obj.AddConstraint("reader")

	data, err := ref.Deref()
	if err != nil {
		t.Errorf("deref failed: %v", err)
	}
	if data != "test data" {
		t.Errorf("expected 'test data', got %v", data)
	}
}

func TestConstraintDerefAfterRelease(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	ref, _ := obj.AddConstraint("reader")
	ref.Release()

	_, err := ref.Deref()
	if err == nil {
		t.Error("deref after release should fail")
	}
}

func TestConstraintDerefAfterFree(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	ref, _ := obj.AddConstraint("reader")
	ref.Release()
	ctx.Free(obj)

	_, err := ref.Deref()
	if err == nil {
		t.Error("deref after free should fail")
	}
}

func TestConstraintDoubleFree(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	ctx.Free(obj)

	err := ctx.Free(obj)
	if err == nil {
		t.Error("double free should fail")
	}
	if !strings.Contains(err.Error(), "double free") {
		t.Errorf("error should mention double free: %v", err)
	}
}

func TestConstraintDoubleRelease(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	ref, _ := obj.AddConstraint("observer")

	ref.Release()
	err := ref.Release()
	if err == nil {
		t.Error("double release should fail")
	}
}

func TestConstraintIsValid(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	ref, _ := obj.AddConstraint("observer")

	if !ref.IsValid() {
		t.Error("ref should be valid")
	}

	ref.Release()
	if ref.IsValid() {
		t.Error("ref should be invalid after release")
	}
}

func TestConstraintAddToFreed(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	ctx.Free(obj)

	_, err := obj.AddConstraint("late observer")
	if err == nil {
		t.Error("should not be able to add constraint to freed object")
	}
}

func TestConstraintStats(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj1 := ctx.Alloc("data1", "owner1")
	obj2 := ctx.Alloc("data2", "owner2")
	obj1.AddConstraint("obs1")
	obj1.AddConstraint("obs2")
	obj2.AddConstraint("obs3")

	stats := ctx.GetStats()
	if stats.TotalObjects != 2 {
		t.Errorf("expected 2 total objects, got %d", stats.TotalObjects)
	}
	if stats.ActiveObjects != 2 {
		t.Errorf("expected 2 active objects, got %d", stats.ActiveObjects)
	}
	if stats.TotalConstraints != 3 {
		t.Errorf("expected 3 total constraints, got %d", stats.TotalConstraints)
	}
}

func TestConstraintViolationLog(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	obj.AddConstraint("observer1")

	ctx.Free(obj) // Will fail and log

	violations := ctx.GetViolations()
	if len(violations) != 1 {
		t.Errorf("expected 1 violation, got %d", len(violations))
	}

	ctx.ClearViolations()
	if ctx.HasViolations() {
		t.Error("violations should be cleared")
	}
}

func TestConstraintMustFree(t *testing.T) {
	ctx := NewConstraintContext(false)

	obj := ctx.Alloc("data", "owner")
	obj.AddConstraint("observer")

	defer func() {
		if r := recover(); r == nil {
			t.Error("MustFree should panic with active constraints")
		}
	}()

	ctx.MustFree(obj)
}

func TestConstraintMultipleObservers(t *testing.T) {
	ctx := NewConstraintContext(false)

	// Simulate observer pattern
	subject := ctx.Alloc("subject", "subject owner")

	observers := make([]*ConstraintRef, 5)
	for i := 0; i < 5; i++ {
		ref, _ := subject.AddConstraint("observer")
		observers[i] = ref
	}

	if subject.ConstraintCount != 5 {
		t.Errorf("expected 5 constraints, got %d", subject.ConstraintCount)
	}

	// Unsubscribe all
	for _, obs := range observers {
		obs.Release()
	}

	// Now can free
	err := ctx.Free(subject)
	if err != nil {
		t.Errorf("free should succeed: %v", err)
	}
}

// ============ Complex Tests ============

func TestConstraintComplexObserverPattern(t *testing.T) {
	ctx := NewConstraintContext(false)

	// Multiple subjects, multiple observers, cross-subscriptions
	subjects := make([]*ConstraintObj, 3)
	for i := 0; i < 3; i++ {
		subjects[i] = ctx.Alloc(i, "subject")
	}

	// Each observer subscribes to multiple subjects
	type Observer struct {
		refs []*ConstraintRef
	}
	observers := make([]*Observer, 5)
	for i := 0; i < 5; i++ {
		observers[i] = &Observer{refs: make([]*ConstraintRef, 0)}
		// Subscribe to all subjects
		for _, subj := range subjects {
			ref, _ := subj.AddConstraint("observer")
			observers[i].refs = append(observers[i].refs, ref)
		}
	}

	// Each subject should have 5 constraints
	for i, subj := range subjects {
		if subj.ConstraintCount != 5 {
			t.Errorf("subject %d should have 5 constraints, got %d", i, subj.ConstraintCount)
		}
	}

	// Observer 2 unsubscribes from all
	for _, ref := range observers[2].refs {
		ref.Release()
	}

	// Now each subject should have 4 constraints
	for i, subj := range subjects {
		if subj.ConstraintCount != 4 {
			t.Errorf("subject %d should have 4 constraints, got %d", i, subj.ConstraintCount)
		}
	}

	// Try to free subject 0 - should fail (4 observers still)
	err := ctx.Free(subjects[0])
	if err == nil {
		t.Error("should not be able to free with active constraints")
	}

	// All remaining observers unsubscribe from subject 0
	for i, obs := range observers {
		if i == 2 {
			continue // Already unsubscribed
		}
		obs.refs[0].Release() // First ref is to subject 0
	}

	// Now subject 0 can be freed
	err = ctx.Free(subjects[0])
	if err != nil {
		t.Errorf("should be able to free now: %v", err)
	}
}

func TestConstraintGraphPattern(t *testing.T) {
	ctx := NewConstraintContext(false)

	// Create a graph where nodes have constraint refs to neighbors
	// A -> B -> C -> A (cycle of constraints)

	nodeA := ctx.Alloc("A", "graph")
	nodeB := ctx.Alloc("B", "graph")
	nodeC := ctx.Alloc("C", "graph")

	refAB, _ := nodeB.AddConstraint("edge A->B")
	refBC, _ := nodeC.AddConstraint("edge B->C")
	refCA, _ := nodeA.AddConstraint("edge C->A")

	// Cannot free any node while cycle exists
	if ctx.Free(nodeA) == nil {
		t.Error("A should not be freeable")
	}
	if ctx.Free(nodeB) == nil {
		t.Error("B should not be freeable")
	}
	if ctx.Free(nodeC) == nil {
		t.Error("C should not be freeable")
	}

	// Break the cycle by releasing one edge
	refCA.Release()

	// Now can free A (no constraints), but B and C still constrained
	if ctx.Free(nodeA) != nil {
		t.Error("A should be freeable after breaking cycle")
	}

	// Release remaining edges
	refAB.Release()
	refBC.Release()

	// Now B and C can be freed
	if ctx.Free(nodeB) != nil {
		t.Error("B should be freeable")
	}
	if ctx.Free(nodeC) != nil {
		t.Error("C should be freeable")
	}
}

func TestConstraintCallbackRegistration(t *testing.T) {
	ctx := NewConstraintContext(false)

	// Simulate callback registration pattern
	type EventEmitter struct {
		obj       *ConstraintObj
		callbacks []*ConstraintRef
	}

	emitter := &EventEmitter{
		obj:       ctx.Alloc("emitter", "event system"),
		callbacks: make([]*ConstraintRef, 0),
	}

	// Register 100 callbacks
	for i := 0; i < 100; i++ {
		ref, err := emitter.obj.AddConstraint("callback")
		if err != nil {
			t.Fatalf("failed to add callback %d: %v", i, err)
		}
		emitter.callbacks = append(emitter.callbacks, ref)
	}

	stats := ctx.GetStats()
	if stats.TotalConstraints != 100 {
		t.Errorf("expected 100 constraints, got %d", stats.TotalConstraints)
	}

	// Unregister half
	for i := 0; i < 50; i++ {
		emitter.callbacks[i].Release()
	}

	if emitter.obj.ConstraintCount != 50 {
		t.Errorf("expected 50 remaining, got %d", emitter.obj.ConstraintCount)
	}

	// Try to free - should fail
	if ctx.Free(emitter.obj) == nil {
		t.Error("should not free with 50 callbacks")
	}

	// Unregister remaining
	for i := 50; i < 100; i++ {
		emitter.callbacks[i].Release()
	}

	// Now can free
	if ctx.Free(emitter.obj) != nil {
		t.Error("should be able to free with 0 callbacks")
	}
}

func TestConstraintHierarchicalOwnership(t *testing.T) {
	ctx := NewConstraintContext(false)

	// Parent owns children, but children have constraint refs to parent
	parent := ctx.Alloc("parent", "tree root")
	children := make([]*ConstraintObj, 5)
	childRefs := make([]*ConstraintRef, 5)

	for i := 0; i < 5; i++ {
		children[i] = ctx.Alloc(i, "child")
		// Child has constraint ref to parent (for "back pointer")
		childRefs[i], _ = parent.AddConstraint("child back-ref")
	}

	// Cannot free parent while children exist with back-refs
	if ctx.Free(parent) == nil {
		t.Error("parent should not be freeable")
	}

	// Free children (release their back-refs first)
	for i := 0; i < 5; i++ {
		childRefs[i].Release()
		if ctx.Free(children[i]) != nil {
			t.Errorf("child %d should be freeable", i)
		}
	}

	// Now parent can be freed
	if ctx.Free(parent) != nil {
		t.Error("parent should be freeable after children")
	}
}

func TestConstraintMixedViolations(t *testing.T) {
	ctx := NewConstraintContext(false)

	// Create multiple objects with constraints
	objs := make([]*ConstraintObj, 5)
	for i := 0; i < 5; i++ {
		objs[i] = ctx.Alloc(i, "mixed")
		// Add varying number of constraints
		for j := 0; j <= i; j++ {
			objs[i].AddConstraint("constraint")
		}
	}

	// Try to free all - should all fail and record violations
	for i := 0; i < 5; i++ {
		ctx.Free(objs[i])
	}

	// Should have 5 violations
	violations := ctx.GetViolations()
	if len(violations) != 5 {
		t.Errorf("expected 5 violations, got %d", len(violations))
	}

	// Clear and verify
	ctx.ClearViolations()
	if ctx.HasViolations() {
		t.Error("should have no violations after clear")
	}
}

func TestConstraintResourcePool(t *testing.T) {
	ctx := NewConstraintContext(false)

	// Simulate a resource pool with borrowing
	type Pool struct {
		resources []*ConstraintObj
		borrowed  map[int]*ConstraintRef
	}

	pool := &Pool{
		resources: make([]*ConstraintObj, 10),
		borrowed:  make(map[int]*ConstraintRef),
	}

	// Create pool resources
	for i := 0; i < 10; i++ {
		pool.resources[i] = ctx.Alloc(i, "pool")
	}

	// Borrow resources
	borrow := func(id int) *ConstraintRef {
		if id >= len(pool.resources) {
			return nil
		}
		ref, _ := pool.resources[id].AddConstraint("borrowed")
		pool.borrowed[id] = ref
		return ref
	}

	returnResource := func(id int) {
		if ref, ok := pool.borrowed[id]; ok {
			ref.Release()
			delete(pool.borrowed, id)
		}
	}

	// Borrow all even-numbered resources
	for i := 0; i < 10; i += 2 {
		borrow(i)
	}

	// Try to free borrowed resource - should fail
	if ctx.Free(pool.resources[0]) == nil {
		t.Error("borrowed resource should not be freeable")
	}

	// Return resource 0
	returnResource(0)

	// Now can free resource 0
	if ctx.Free(pool.resources[0]) != nil {
		t.Error("returned resource should be freeable")
	}

	// Free unborrowed resources
	for i := 1; i < 10; i += 2 {
		if ctx.Free(pool.resources[i]) != nil {
			t.Errorf("unborrowed resource %d should be freeable", i)
		}
	}
}

func TestConstraintConcurrentLikePattern(t *testing.T) {
	ctx := NewConstraintContext(false)

	// Simulate concurrent-like access with constraints
	sharedData := ctx.Alloc("shared", "main")

	// Multiple "workers" acquire constraints
	workers := make([]*ConstraintRef, 10)
	for i := 0; i < 10; i++ {
		workers[i], _ = sharedData.AddConstraint("worker")
	}

	// Stats should show 10 constraints
	stats := ctx.GetStats()
	if stats.TotalConstraints != 10 {
		t.Errorf("expected 10 constraints, got %d", stats.TotalConstraints)
	}

	// Workers release in random-ish order
	order := []int{3, 7, 1, 9, 0, 5, 2, 8, 4, 6}
	for _, i := range order {
		workers[i].Release()
	}

	// All released, can free
	if ctx.Free(sharedData) != nil {
		t.Error("should be freeable after all workers release")
	}
}

func TestConstraintNestedOwnership(t *testing.T) {
	ctx := NewConstraintContext(false)

	// Deeply nested ownership with constraints at each level
	// Level 0 -> Level 1 -> Level 2 -> Level 3
	levels := make([]*ConstraintObj, 4)
	refs := make([]*ConstraintRef, 3)

	for i := 0; i < 4; i++ {
		levels[i] = ctx.Alloc(i, "level")
		if i > 0 {
			// Each level has constraint to previous
			refs[i-1], _ = levels[i-1].AddConstraint("level constraint")
		}
	}

	// Cannot free level 0 (constrained by level 1's ref)
	if ctx.Free(levels[0]) == nil {
		t.Error("level 0 should not be freeable")
	}

	// Release constraints bottom-up
	for i := 2; i >= 0; i-- {
		refs[i].Release()
	}

	// Now can free all top-down
	for i := 0; i < 4; i++ {
		if ctx.Free(levels[i]) != nil {
			t.Errorf("level %d should be freeable", i)
		}
	}
}
