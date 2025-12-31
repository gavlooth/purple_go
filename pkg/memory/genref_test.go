package memory

import (
	"strings"
	"testing"
)

func TestGenRefBasicAllocation(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("hello")
	if obj.Data != "hello" {
		t.Errorf("expected 'hello', got %v", obj.Data)
	}
	if obj.Generation == 0 {
		t.Error("generation should not be 0")
	}
	if obj.Freed {
		t.Error("should not be freed")
	}
}

func TestGenRefCreateAndDeref(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("test data")
	ref, err := obj.CreateRef("test location")
	if err != nil {
		t.Errorf("unexpected error: %v", err)
	}

	data, err := ref.Deref()
	if err != nil {
		t.Errorf("deref failed: %v", err)
	}
	if data != "test data" {
		t.Errorf("expected 'test data', got %v", data)
	}
}

func TestGenRefUseAfterFree(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("will be freed")
	ref, _ := obj.CreateRef("before free")

	// Free the object
	obj.Free()

	// Deref should fail with use-after-free
	_, err := ref.Deref()
	if err == nil {
		t.Error("expected use-after-free error")
	}
	if !strings.Contains(err.Error(), "use-after-free") {
		t.Errorf("error should mention use-after-free: %v", err)
	}
}

func TestGenRefIsValid(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("data")
	ref, _ := obj.CreateRef("test")

	if !ref.IsValid() {
		t.Error("ref should be valid before free")
	}

	obj.Free()

	if ref.IsValid() {
		t.Error("ref should be invalid after free")
	}
}

func TestGenRefCannotCreateRefToFreed(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("data")
	obj.Free()

	_, err := obj.CreateRef("after free")
	if err == nil {
		t.Error("should not be able to create ref to freed object")
	}
}

func TestGenRefMultipleRefs(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("shared data")
	ref1, _ := obj.CreateRef("ref1")
	ref2, _ := obj.CreateRef("ref2")
	ref3, _ := obj.CreateRef("ref3")

	// All should be valid
	if !ref1.IsValid() || !ref2.IsValid() || !ref3.IsValid() {
		t.Error("all refs should be valid")
	}

	// Free invalidates all
	obj.Free()

	if ref1.IsValid() || ref2.IsValid() || ref3.IsValid() {
		t.Error("all refs should be invalid after free")
	}
}

func TestGenRefRandomGenerationUniqueness(t *testing.T) {
	ctx := NewGenRefContext()

	// Allocate many objects and check generation uniqueness
	seen := make(map[Generation]bool)
	for i := 0; i < 1000; i++ {
		obj := ctx.Alloc(i)
		if seen[obj.Generation] {
			// Collision is possible but extremely unlikely (1/2^64)
			t.Logf("warning: generation collision detected (extremely rare)")
		}
		seen[obj.Generation] = true
	}
}

func TestGenClosureValidCaptures(t *testing.T) {
	ctx := NewGenRefContext()

	obj1 := ctx.Alloc(10)
	obj2 := ctx.Alloc(20)

	ref1, _ := obj1.CreateRef("capture1")
	ref2, _ := obj2.CreateRef("capture2")

	closure := NewGenClosure([]*GenRef{ref1, ref2}, func() interface{} {
		v1, _ := ref1.Deref()
		v2, _ := ref2.Deref()
		return v1.(int) + v2.(int)
	})

	result, err := closure.Call()
	if err != nil {
		t.Errorf("closure call failed: %v", err)
	}
	if result != 30 {
		t.Errorf("expected 30, got %v", result)
	}
}

func TestGenClosureInvalidCapture(t *testing.T) {
	ctx := NewGenRefContext()

	obj1 := ctx.Alloc(10)
	obj2 := ctx.Alloc(20)

	ref1, _ := obj1.CreateRef("capture1")
	ref2, _ := obj2.CreateRef("capture2")

	closure := NewGenClosure([]*GenRef{ref1, ref2}, func() interface{} {
		return nil
	})

	// Free one of the captured objects
	obj1.Free()

	// Closure call should fail due to invalid capture
	_, err := closure.Call()
	if err == nil {
		t.Error("closure should fail with invalid capture")
	}
	if !strings.Contains(err.Error(), "invalid") {
		t.Errorf("error should mention invalid capture: %v", err)
	}
}

func TestGenClosureValidateCaptures(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("captured")
	ref, _ := obj.CreateRef("closure capture")

	closure := NewGenClosure([]*GenRef{ref}, func() interface{} {
		return nil
	})

	// Should pass validation
	err := closure.ValidateCaptures()
	if err != nil {
		t.Errorf("validation should pass: %v", err)
	}

	// Free and re-validate
	obj.Free()

	err = closure.ValidateCaptures()
	if err == nil {
		t.Error("validation should fail after free")
	}
}

func TestGenRefMustDerefPanics(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("data")
	ref, _ := obj.CreateRef("test")
	obj.Free()

	defer func() {
		if r := recover(); r == nil {
			t.Error("MustDeref should panic on invalid ref")
		}
	}()

	ref.MustDeref() // Should panic
}

func TestGenRefSourceDescription(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("data")
	ref, _ := obj.CreateRef("main.go:42 closure capture")
	obj.Free()

	_, err := ref.Deref()
	if err == nil {
		t.Error("expected error")
	}
	if !strings.Contains(err.Error(), "main.go:42") {
		t.Errorf("error should contain source description: %v", err)
	}
}

func TestGenRefNullReference(t *testing.T) {
	ref := &GenRef{Target: nil}

	_, err := ref.Deref()
	if err == nil {
		t.Error("null ref deref should fail")
	}

	if ref.IsValid() {
		t.Error("null ref should not be valid")
	}
}

// ============ Complex Tests ============

func TestGenRefChainedClosures(t *testing.T) {
	ctx := NewGenRefContext()

	// Create a chain of closures, each capturing the previous
	obj1 := ctx.Alloc(1)
	obj2 := ctx.Alloc(2)
	obj3 := ctx.Alloc(3)

	ref1, _ := obj1.CreateRef("closure1")
	ref2, _ := obj2.CreateRef("closure2")
	ref3, _ := obj3.CreateRef("closure3")

	// Closure that uses all three
	closure := NewGenClosure([]*GenRef{ref1, ref2, ref3}, func() interface{} {
		v1, _ := ref1.Deref()
		v2, _ := ref2.Deref()
		v3, _ := ref3.Deref()
		return v1.(int) + v2.(int) + v3.(int)
	})

	// Should work
	result, err := closure.Call()
	if err != nil {
		t.Errorf("closure should work: %v", err)
	}
	if result != 6 {
		t.Errorf("expected 6, got %v", result)
	}

	// Free middle object
	obj2.Free()

	// Closure should fail now
	_, err = closure.Call()
	if err == nil {
		t.Error("closure should fail with freed capture")
	}
}

func TestGenRefMassiveRefCount(t *testing.T) {
	ctx := NewGenRefContext()

	obj := ctx.Alloc("shared")

	// Create 1000 refs to same object
	refs := make([]*GenRef, 1000)
	for i := 0; i < 1000; i++ {
		ref, err := obj.CreateRef("mass ref")
		if err != nil {
			t.Fatalf("failed to create ref %d: %v", i, err)
		}
		refs[i] = ref
	}

	// All should be valid
	for i, ref := range refs {
		if !ref.IsValid() {
			t.Errorf("ref %d should be valid", i)
		}
	}

	// Free the object
	obj.Free()

	// ALL refs should now be invalid
	invalidCount := 0
	for _, ref := range refs {
		if !ref.IsValid() {
			invalidCount++
		}
	}
	if invalidCount != 1000 {
		t.Errorf("expected 1000 invalid refs, got %d", invalidCount)
	}
}

func TestGenRefNestedClosures(t *testing.T) {
	ctx := NewGenRefContext()

	outer := ctx.Alloc(10)
	inner := ctx.Alloc(20)

	outerRef, _ := outer.CreateRef("outer capture")
	innerRef, _ := inner.CreateRef("inner capture")

	// Outer closure captures outer, returns inner closure
	outerClosure := NewGenClosure([]*GenRef{outerRef}, func() interface{} {
		// This simulates an inner closure that also uses outerRef
		innerClosure := NewGenClosure([]*GenRef{innerRef, outerRef}, func() interface{} {
			v1, _ := innerRef.Deref()
			v2, _ := outerRef.Deref()
			return v1.(int) + v2.(int)
		})
		result, _ := innerClosure.Call()
		return result
	})

	result, err := outerClosure.Call()
	if err != nil {
		t.Errorf("nested closure should work: %v", err)
	}
	if result != 30 {
		t.Errorf("expected 30, got %v", result)
	}

	// Free outer
	outer.Free()

	// Now should fail
	_, err = outerClosure.Call()
	if err == nil {
		t.Error("should fail after outer freed")
	}
}

func TestGenRefGenerationCollision(t *testing.T) {
	ctx := NewGenRefContext()

	// Test that we don't get false positives from generation reuse
	// Create and free many objects, then create new ones
	for round := 0; round < 100; round++ {
		objs := make([]*GenObj, 100)
		refs := make([]*GenRef, 100)

		for i := 0; i < 100; i++ {
			objs[i] = ctx.Alloc(i)
			refs[i], _ = objs[i].CreateRef("test")
		}

		// Free all
		for _, obj := range objs {
			obj.Free()
		}

		// All refs should be invalid
		for i, ref := range refs {
			if ref.IsValid() {
				t.Errorf("round %d: ref %d should be invalid after free", round, i)
			}
		}
	}
}

func TestGenRefCallbackPattern(t *testing.T) {
	ctx := NewGenRefContext()

	// Simulate event listener pattern
	type EventSource struct {
		listeners []*GenClosure
	}

	source := &EventSource{}

	// Create 5 listeners, each capturing its own state
	states := make([]*GenObj, 5)
	for i := 0; i < 5; i++ {
		states[i] = ctx.Alloc(i * 10)
		ref, _ := states[i].CreateRef("listener state")

		localRef := ref // Capture for closure
		localI := i
		listener := NewGenClosure([]*GenRef{localRef}, func() interface{} {
			v, _ := localRef.Deref()
			return v.(int) + localI
		})
		source.listeners = append(source.listeners, listener)
	}

	// Fire all listeners
	for i, listener := range source.listeners {
		result, err := listener.Call()
		if err != nil {
			t.Errorf("listener %d should work: %v", i, err)
		}
		expected := i*10 + i
		if result != expected {
			t.Errorf("listener %d: expected %d, got %v", i, expected, result)
		}
	}

	// Free state for listener 2
	states[2].Free()

	// Now listener 2 should fail, others work
	for i, listener := range source.listeners {
		_, err := listener.Call()
		if i == 2 {
			if err == nil {
				t.Error("listener 2 should fail")
			}
		} else {
			if err != nil {
				t.Errorf("listener %d should still work: %v", i, err)
			}
		}
	}
}

func TestGenRefRefToRef(t *testing.T) {
	ctx := NewGenRefContext()

	// Object containing a reference
	innerObj := ctx.Alloc(42)
	innerRef, _ := innerObj.CreateRef("inner")

	// Outer object holds the reference
	outerObj := ctx.Alloc(innerRef)
	outerRef, _ := outerObj.CreateRef("outer")

	// Dereference chain
	outerData, err := outerRef.Deref()
	if err != nil {
		t.Fatalf("outer deref failed: %v", err)
	}

	extractedRef := outerData.(*GenRef)
	innerData, err := extractedRef.Deref()
	if err != nil {
		t.Fatalf("inner deref failed: %v", err)
	}

	if innerData != 42 {
		t.Errorf("expected 42, got %v", innerData)
	}

	// Free inner - outer deref still works but extracted ref fails
	innerObj.Free()

	outerData2, err := outerRef.Deref()
	if err != nil {
		t.Error("outer should still deref (it holds the GenRef struct)")
	}
	extractedRef2 := outerData2.(*GenRef)
	_, err = extractedRef2.Deref()
	if err == nil {
		t.Error("extracted ref should fail - inner was freed")
	}
}

func TestGenRefRaceSimulation(t *testing.T) {
	ctx := NewGenRefContext()

	// Simulate a race condition scenario
	// In real concurrent code, this would need synchronization
	obj := ctx.Alloc("contested resource")

	// Multiple "threads" create refs
	refs := make([]*GenRef, 10)
	for i := 0; i < 10; i++ {
		refs[i], _ = obj.CreateRef("thread ref")
	}

	// One "thread" frees
	obj.Free()

	// All refs should be atomically invalid
	for i, ref := range refs {
		if ref.IsValid() {
			t.Errorf("ref %d should be invalid", i)
		}
	}
}

func TestGenRefLongLivedVsShortLived(t *testing.T) {
	ctx := NewGenRefContext()

	// Long-lived object
	longLived := ctx.Alloc("persistent")
	longRef, _ := longLived.CreateRef("long lived ref")

	// Many short-lived objects
	for i := 0; i < 100; i++ {
		shortLived := ctx.Alloc(i)
		shortRef, _ := shortLived.CreateRef("short lived")

		// Use both
		if !longRef.IsValid() {
			t.Error("long ref should always be valid")
		}
		if !shortRef.IsValid() {
			t.Error("short ref should be valid before free")
		}

		shortLived.Free()

		if shortRef.IsValid() {
			t.Errorf("short ref %d should be invalid after free", i)
		}
	}

	// Long-lived still valid
	if !longRef.IsValid() {
		t.Error("long ref should still be valid")
	}
}
