package memory

import (
	"testing"
)

// Integration tests that combine multiple safety strategies

func TestIntegration_RegionWithGenRef(t *testing.T) {
	// Combine region scoping with generational references
	regionCtx := NewRegionContext()
	genCtx := NewGenRefContext()

	// Allocate in outer region with gen ref
	outerData := genCtx.Alloc("outer data")
	outerGenRef, _ := outerData.CreateRef("outer region")
	outerRegionObj := regionCtx.Alloc(outerGenRef)

	// Enter inner region
	regionCtx.EnterRegion()
	innerData := genCtx.Alloc("inner data")
	innerGenRef, _ := innerData.CreateRef("inner region")
	innerRegionObj := regionCtx.Alloc(innerGenRef)

	// Inner can reference outer (region allows)
	_, err := regionCtx.CreateRef(innerRegionObj, outerRegionObj)
	if err != nil {
		t.Errorf("inner should reference outer: %v", err)
	}

	// Both gen refs should be valid
	if !outerGenRef.IsValid() || !innerGenRef.IsValid() {
		t.Error("both gen refs should be valid")
	}

	// Exit inner region
	regionCtx.ExitRegion()

	// Inner region obj is invalid, but outer still valid
	if innerRegionObj.Region != nil {
		t.Error("inner region obj should be invalid")
	}
	if outerRegionObj.Region == nil {
		t.Error("outer region obj should still be valid")
	}

	// Gen refs are independent - both still valid until freed
	if !outerGenRef.IsValid() {
		t.Error("outer gen ref should still be valid")
	}
	if !innerGenRef.IsValid() {
		t.Error("inner gen ref should still be valid (gen obj not freed)")
	}

	// Free inner gen obj
	innerData.Free()
	if innerGenRef.IsValid() {
		t.Error("inner gen ref should be invalid after free")
	}
}

func TestIntegration_GenRefWithConstraint(t *testing.T) {
	// Combine generational refs with constraint refs
	genCtx := NewGenRefContext()
	conCtx := NewConstraintContext(false)

	// Create gen object
	genObj := genCtx.Alloc("shared data")
	genRef, _ := genObj.CreateRef("gen ref")

	// Create constraint object that "observes" the gen ref
	observer := conCtx.Alloc(genRef, "observer")
	constraint, _ := observer.AddConstraint("observation")

	// Both should be valid
	if !genRef.IsValid() {
		t.Error("gen ref should be valid")
	}
	if !constraint.IsValid() {
		t.Error("constraint should be valid")
	}

	// Cannot free observer while constraint exists
	if conCtx.Free(observer) == nil {
		t.Error("should not free with active constraint")
	}

	// Release constraint, then free
	constraint.Release()
	if conCtx.Free(observer) != nil {
		t.Error("should be able to free after release")
	}

	// Gen ref still valid (independent)
	if !genRef.IsValid() {
		t.Error("gen ref should still be valid")
	}

	// Free gen obj
	genObj.Free()
	if genRef.IsValid() {
		t.Error("gen ref should be invalid after free")
	}
}

func TestIntegration_AllThreeStrategies(t *testing.T) {
	// Use all three strategies together
	regionCtx := NewRegionContext()
	genCtx := NewGenRefContext()
	conCtx := NewConstraintContext(false)

	// Root level: constraint-managed owner
	owner := conCtx.Alloc("owner", "main")

	// Gen object with ref
	data := genCtx.Alloc(42)
	dataRef, _ := data.CreateRef("captured data")

	// Region-scoped accessor
	accessor := regionCtx.Alloc(struct {
		genRef     *GenRef
		constraint *ConstraintRef
	}{
		genRef:     dataRef,
		constraint: nil,
	})

	// Add constraint from accessor to owner
	constraint, _ := owner.AddConstraint("accessor constraint")
	accessorData := accessor.Data.(struct {
		genRef     *GenRef
		constraint *ConstraintRef
	})
	accessorData.constraint = constraint
	accessor.Data = accessorData

	// Verify all valid
	if !dataRef.IsValid() {
		t.Error("gen ref should be valid")
	}
	if !constraint.IsValid() {
		t.Error("constraint should be valid")
	}
	if accessor.Region == nil {
		t.Error("accessor should be in valid region")
	}

	// Enter nested region, create another accessor
	regionCtx.EnterRegion()
	nestedAccessor := regionCtx.Alloc("nested")

	// Nested can reference root accessor (inner->outer allowed)
	_, err := regionCtx.CreateRef(nestedAccessor, accessor)
	if err != nil {
		t.Errorf("nested should reference accessor: %v", err)
	}

	// Exit nested region
	regionCtx.ExitRegion()

	// Nested accessor invalid, but root accessor still valid
	if nestedAccessor.Region != nil {
		t.Error("nested accessor should be invalid")
	}
	if accessor.Region == nil {
		t.Error("root accessor should still be valid")
	}

	// Release constraint and free owner
	constraint.Release()
	if conCtx.Free(owner) != nil {
		t.Error("owner should be freeable")
	}

	// Free gen data
	data.Free()
	if dataRef.IsValid() {
		t.Error("gen ref should be invalid")
	}
}

func TestIntegration_ClosureWithRegionAndConstraint(t *testing.T) {
	// Simulate a closure that captures region-scoped data with constraint
	regionCtx := NewRegionContext()
	genCtx := NewGenRefContext()
	conCtx := NewConstraintContext(false)

	// Create data in current region
	capturedValue := genCtx.Alloc(100)
	capturedRef, _ := capturedValue.CreateRef("closure capture")

	// Region object holding the capture
	regionCtx.EnterRegion()
	holder := regionCtx.Alloc(capturedRef)

	// Constraint on the captured value
	constraintOwner := conCtx.Alloc("closure owner", "lambda")
	closureConstraint, _ := constraintOwner.AddConstraint("closure holds ref")

	// Create closure that uses gen ref
	closure := NewGenClosure([]*GenRef{capturedRef}, func() interface{} {
		v, _ := capturedRef.Deref()
		return v.(int) * 2
	})

	// Execute closure
	result, err := closure.Call()
	if err != nil {
		t.Errorf("closure should work: %v", err)
	}
	if result != 200 {
		t.Errorf("expected 200, got %v", result)
	}

	// Exit region - holder invalid but closure still works
	regionCtx.ExitRegion()
	if holder.Region != nil {
		t.Error("holder should be invalid")
	}

	// Closure still works (gen ref independent of region)
	result, err = closure.Call()
	if err != nil {
		t.Errorf("closure should still work: %v", err)
	}
	if result != 200 {
		t.Errorf("expected 200, got %v", result)
	}

	// Free captured value - closure should fail
	capturedValue.Free()
	_, err = closure.Call()
	if err == nil {
		t.Error("closure should fail after capture freed")
	}

	// Constraint still active - cannot free owner
	if conCtx.Free(constraintOwner) == nil {
		t.Error("should not free with active constraint")
	}

	// Release and free
	closureConstraint.Release()
	if conCtx.Free(constraintOwner) != nil {
		t.Error("should be able to free after release")
	}
}

func TestIntegration_ComplexOwnershipGraph(t *testing.T) {
	// Complex ownership: Region hierarchy + Gen refs + Constraint tracking
	//
	//   Root Region
	//   ├── Node A (gen ref) ──constraint──> Observer 1
	//   │   └── Inner Region
	//   │       └── Node B (gen ref) ──constraint──> Observer 2
	//   └── Node C (gen ref) ──constraint──> Observer 1, Observer 2

	regionCtx := NewRegionContext()
	genCtx := NewGenRefContext()
	conCtx := NewConstraintContext(false)

	// Observers
	observer1 := conCtx.Alloc("observer1", "obs1")
	observer2 := conCtx.Alloc("observer2", "obs2")

	// Root level nodes
	nodeA := genCtx.Alloc("A")
	refA, _ := nodeA.CreateRef("node A")
	regionObjA := regionCtx.Alloc(refA)
	constraintA1, _ := observer1.AddConstraint("A observes 1")

	nodeC := genCtx.Alloc("C")
	refC, _ := nodeC.CreateRef("node C")
	regionCtx.Alloc(refC)
	constraintC1, _ := observer1.AddConstraint("C observes 1")
	constraintC2, _ := observer2.AddConstraint("C observes 2")

	// Inner region with node B
	regionCtx.EnterRegion()
	nodeB := genCtx.Alloc("B")
	refB, _ := nodeB.CreateRef("node B")
	regionObjB := regionCtx.Alloc(refB)
	constraintB2, _ := observer2.AddConstraint("B observes 2")

	// B can reference A (inner -> outer)
	_, err := regionCtx.CreateRef(regionObjB, regionObjA)
	if err != nil {
		t.Errorf("B should reference A: %v", err)
	}

	// Verify all constraints
	if observer1.ConstraintCount != 2 {
		t.Errorf("observer1 should have 2 constraints, got %d", observer1.ConstraintCount)
	}
	if observer2.ConstraintCount != 2 {
		t.Errorf("observer2 should have 2 constraints, got %d", observer2.ConstraintCount)
	}

	// Exit inner region
	regionCtx.ExitRegion()

	// B's region obj invalid, but gen ref still works
	if regionObjB.Region != nil {
		t.Error("B region obj should be invalid")
	}
	if !refB.IsValid() {
		t.Error("B gen ref should still be valid")
	}

	// Release B's constraint
	constraintB2.Release()

	// Free node B
	nodeB.Free()
	if refB.IsValid() {
		t.Error("B gen ref should be invalid after free")
	}

	// Observer 2 now has 1 constraint (from C)
	if observer2.ConstraintCount != 1 {
		t.Errorf("observer2 should have 1 constraint, got %d", observer2.ConstraintCount)
	}

	// Release remaining constraints
	constraintA1.Release()
	constraintC1.Release()
	constraintC2.Release()

	// Free observers
	if conCtx.Free(observer1) != nil {
		t.Error("observer1 should be freeable")
	}
	if conCtx.Free(observer2) != nil {
		t.Error("observer2 should be freeable")
	}

	// Free remaining gen objects
	nodeA.Free()
	nodeC.Free()
}

func TestIntegration_StressTest(t *testing.T) {
	// Stress test with many objects across all strategies
	regionCtx := NewRegionContext()
	genCtx := NewGenRefContext()
	conCtx := NewConstraintContext(false)

	const (
		numRegionLevels = 5
		objsPerLevel    = 20
		constraintsPer  = 3
	)

	type TrackedObj struct {
		genObj      *GenObj
		genRef      *GenRef
		regionObj   *RegionObj
		constraints []*ConstraintRef
	}

	allObjects := make([][]*TrackedObj, numRegionLevels)
	constraintOwner := conCtx.Alloc("constraint owner", "stress test")

	// Create nested regions with objects
	for level := 0; level < numRegionLevels; level++ {
		if level > 0 {
			regionCtx.EnterRegion()
		}

		allObjects[level] = make([]*TrackedObj, objsPerLevel)
		for i := 0; i < objsPerLevel; i++ {
			obj := &TrackedObj{}
			obj.genObj = genCtx.Alloc(level*100 + i)
			obj.genRef, _ = obj.genObj.CreateRef("stress test")
			obj.regionObj = regionCtx.Alloc(obj.genRef)
			obj.constraints = make([]*ConstraintRef, constraintsPer)
			for c := 0; c < constraintsPer; c++ {
				obj.constraints[c], _ = constraintOwner.AddConstraint("stress")
			}
			allObjects[level][i] = obj
		}
	}

	// Verify all valid
	for level, objs := range allObjects {
		for i, obj := range objs {
			if !obj.genRef.IsValid() {
				t.Errorf("level %d obj %d gen ref should be valid", level, i)
			}
			if obj.regionObj.Region == nil {
				t.Errorf("level %d obj %d region obj should be valid", level, i)
			}
		}
	}

	// Exit regions from innermost to outermost
	for level := numRegionLevels - 1; level > 0; level-- {
		regionCtx.ExitRegion()

		// Objects at this level should have invalid region
		for i, obj := range allObjects[level] {
			if obj.regionObj.Region != nil {
				t.Errorf("after exit, level %d obj %d region should be invalid", level, i)
			}
			// But gen ref still valid
			if !obj.genRef.IsValid() {
				t.Errorf("after exit, level %d obj %d gen ref should still be valid", level, i)
			}
		}
	}

	// Release all constraints
	for _, levelObjs := range allObjects {
		for _, obj := range levelObjs {
			for _, c := range obj.constraints {
				c.Release()
			}
		}
	}

	// Free all gen objects
	for _, levelObjs := range allObjects {
		for _, obj := range levelObjs {
			obj.genObj.Free()
			if obj.genRef.IsValid() {
				t.Error("gen ref should be invalid after free")
			}
		}
	}

	// Free constraint owner
	if conCtx.Free(constraintOwner) != nil {
		t.Error("constraint owner should be freeable")
	}
}
