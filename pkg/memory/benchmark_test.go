package memory

import (
	"testing"
)

// ============ Region Reference Benchmarks ============

func BenchmarkRegion_Alloc(b *testing.B) {
	ctx := NewRegionContext()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx.Alloc(i)
	}
}

func BenchmarkRegion_EnterExit(b *testing.B) {
	ctx := NewRegionContext()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx.EnterRegion()
		ctx.ExitRegion()
	}
}

func BenchmarkRegion_CreateRef(b *testing.B) {
	ctx := NewRegionContext()
	outer := ctx.Alloc("outer")
	ctx.EnterRegion()
	inner := ctx.Alloc("inner")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx.CreateRef(inner, outer)
	}
}

func BenchmarkRegion_CanReference(b *testing.B) {
	ctx := NewRegionContext()
	outer := ctx.Alloc("outer")
	ctx.EnterRegion()
	inner := ctx.Alloc("inner")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		CanReference(inner, outer)
	}
}

func BenchmarkRegion_DeepNesting(b *testing.B) {
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx := NewRegionContext()
		for j := 0; j < 10; j++ {
			ctx.EnterRegion()
			ctx.Alloc(j)
		}
		for j := 0; j < 10; j++ {
			ctx.ExitRegion()
		}
	}
}

// ============ Generational Reference Benchmarks ============

func BenchmarkGenRef_Alloc(b *testing.B) {
	ctx := NewGenRefContext()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx.Alloc(i)
	}
}

func BenchmarkGenRef_CreateRef(b *testing.B) {
	ctx := NewGenRefContext()
	obj := ctx.Alloc("data")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		obj.CreateRef("benchmark")
	}
}

func BenchmarkGenRef_Deref(b *testing.B) {
	ctx := NewGenRefContext()
	obj := ctx.Alloc("data")
	ref, _ := obj.CreateRef("benchmark")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ref.Deref()
	}
}

func BenchmarkGenRef_IsValid(b *testing.B) {
	ctx := NewGenRefContext()
	obj := ctx.Alloc("data")
	ref, _ := obj.CreateRef("benchmark")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ref.IsValid()
	}
}

func BenchmarkGenRef_Free(b *testing.B) {
	ctx := NewGenRefContext()
	objs := make([]*GenObj, b.N)
	for i := 0; i < b.N; i++ {
		objs[i] = ctx.Alloc(i)
	}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		objs[i].Free()
	}
}

func BenchmarkGenRef_RandomGeneration(b *testing.B) {
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		randomGeneration()
	}
}

func BenchmarkGenRef_ClosureCall(b *testing.B) {
	ctx := NewGenRefContext()
	obj := ctx.Alloc(42)
	ref, _ := obj.CreateRef("closure")
	closure := NewGenClosure([]*GenRef{ref}, func() interface{} {
		return nil
	})
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		closure.Call()
	}
}

func BenchmarkGenRef_ClosureValidate(b *testing.B) {
	ctx := NewGenRefContext()
	obj := ctx.Alloc(42)
	ref, _ := obj.CreateRef("closure")
	closure := NewGenClosure([]*GenRef{ref}, func() interface{} {
		return nil
	})
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		closure.ValidateCaptures()
	}
}

// ============ Constraint Reference Benchmarks ============

func BenchmarkConstraint_Alloc(b *testing.B) {
	ctx := NewConstraintContext(false)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx.Alloc(i, "owner")
	}
}

func BenchmarkConstraint_AddConstraint(b *testing.B) {
	ctx := NewConstraintContext(false)
	obj := ctx.Alloc("data", "owner")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		obj.AddConstraint("benchmark")
	}
}

func BenchmarkConstraint_Release(b *testing.B) {
	ctx := NewConstraintContext(false)
	obj := ctx.Alloc("data", "owner")
	refs := make([]*ConstraintRef, b.N)
	for i := 0; i < b.N; i++ {
		refs[i], _ = obj.AddConstraint("benchmark")
	}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		refs[i].Release()
	}
}

func BenchmarkConstraint_Deref(b *testing.B) {
	ctx := NewConstraintContext(false)
	obj := ctx.Alloc("data", "owner")
	ref, _ := obj.AddConstraint("benchmark")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ref.Deref()
	}
}

func BenchmarkConstraint_IsValid(b *testing.B) {
	ctx := NewConstraintContext(false)
	obj := ctx.Alloc("data", "owner")
	ref, _ := obj.AddConstraint("benchmark")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ref.IsValid()
	}
}

func BenchmarkConstraint_Free(b *testing.B) {
	ctx := NewConstraintContext(false)
	objs := make([]*ConstraintObj, b.N)
	for i := 0; i < b.N; i++ {
		objs[i] = ctx.Alloc(i, "owner")
	}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx.Free(objs[i])
	}
}

// ============ Symmetric RC Benchmarks ============

func BenchmarkSymmetricRC_Alloc(b *testing.B) {
	scope := NewSymmetricScope(nil)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		NewSymmetricObj(i)
	}
	_ = scope
}

func BenchmarkSymmetricRC_ScopeOwn(b *testing.B) {
	scope := NewSymmetricScope(nil)
	objs := make([]*SymmetricObj, b.N)
	for i := 0; i < b.N; i++ {
		objs[i] = NewSymmetricObj(i)
	}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		scope.Own(objs[i])
	}
}

func BenchmarkSymmetricRC_Link(b *testing.B) {
	scope := NewSymmetricScope(nil)
	obj1 := NewSymmetricObj(1)
	obj2 := NewSymmetricObj(2)
	scope.Own(obj1)
	scope.Own(obj2)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		SymmetricIncRef(obj1, obj2)
	}
}

// ============ Comparison Benchmarks ============

// Baseline: raw pointer dereference (no safety)
func BenchmarkBaseline_PointerDeref(b *testing.B) {
	data := 42
	ptr := &data
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = *ptr
	}
}

// Baseline: raw allocation
func BenchmarkBaseline_Alloc(b *testing.B) {
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = new(int)
	}
}

// Compare: GenRef deref vs raw pointer
func BenchmarkCompare_GenRefVsRaw(b *testing.B) {
	ctx := NewGenRefContext()
	obj := ctx.Alloc(42)
	ref, _ := obj.CreateRef("compare")

	b.Run("Raw", func(b *testing.B) {
		data := 42
		ptr := &data
		for i := 0; i < b.N; i++ {
			_ = *ptr
		}
	})

	b.Run("GenRef", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			ref.Deref()
		}
	})

	b.Run("GenRef_IsValid", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			ref.IsValid()
		}
	})
}

// Compare: Region ref check vs no check
func BenchmarkCompare_RegionCheck(b *testing.B) {
	ctx := NewRegionContext()
	outer := ctx.Alloc("outer")
	ctx.EnterRegion()
	inner := ctx.Alloc("inner")

	b.Run("NoCheck", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			_ = inner.Region.Depth >= outer.Region.Depth
		}
	})

	b.Run("CanReference", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			CanReference(inner, outer)
		}
	})
}

// ============ Realistic Workload Benchmarks ============

func BenchmarkWorkload_ClosureHeavy(b *testing.B) {
	ctx := NewGenRefContext()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		// Create object
		obj := ctx.Alloc(i)
		ref, _ := obj.CreateRef("workload")

		// Create closure
		closure := NewGenClosure([]*GenRef{ref}, func() interface{} {
			v, _ := ref.Deref()
			return v
		})

		// Call closure
		closure.Call()

		// Free
		obj.Free()
	}
}

func BenchmarkWorkload_ObserverPattern(b *testing.B) {
	ctx := NewConstraintContext(false)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		// Create subject
		subject := ctx.Alloc(i, "subject")

		// Add 5 observers
		refs := make([]*ConstraintRef, 5)
		for j := 0; j < 5; j++ {
			refs[j], _ = subject.AddConstraint("observer")
		}

		// Release observers
		for _, ref := range refs {
			ref.Release()
		}

		// Free subject
		ctx.Free(subject)
	}
}

func BenchmarkWorkload_ScopedAccess(b *testing.B) {
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx := NewRegionContext()

		// Outer data
		outer := ctx.Alloc("shared")

		// Inner scope accesses outer
		ctx.EnterRegion()
		inner := ctx.Alloc("local")
		ctx.CreateRef(inner, outer)
		ctx.ExitRegion()
	}
}

func BenchmarkWorkload_MixedStrategies(b *testing.B) {
	regionCtx := NewRegionContext()
	genCtx := NewGenRefContext()
	conCtx := NewConstraintContext(false)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		// Gen object with constraint
		obj := genCtx.Alloc(i)
		ref, _ := obj.CreateRef("mixed")

		// Region-scoped holder
		regionCtx.EnterRegion()
		holder := regionCtx.Alloc(ref)
		_ = holder

		// Constraint owner
		owner := conCtx.Alloc("data", "owner")
		constraint, _ := owner.AddConstraint("ref")

		// Cleanup
		constraint.Release()
		conCtx.Free(owner)
		regionCtx.ExitRegion()
		obj.Free()
	}
}
