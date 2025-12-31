package memory

import "testing"

func TestSymmetricRC_SimpleAlloc(t *testing.T) {
	ctx := NewSymmetricContext()

	obj := ctx.Alloc("test data")

	if obj.ExternalRC != 1 {
		t.Errorf("Expected external_rc=1, got %d", obj.ExternalRC)
	}
	if obj.InternalRC != 0 {
		t.Errorf("Expected internal_rc=0, got %d", obj.InternalRC)
	}
	if obj.Freed {
		t.Error("Object should not be freed yet")
	}
}

func TestSymmetricRC_ScopeRelease(t *testing.T) {
	ctx := NewSymmetricContext()

	ctx.EnterScope()
	obj := ctx.Alloc("test")
	ctx.ExitScope()

	if !obj.Freed {
		t.Error("Object should be freed after scope exit")
	}
}

func TestSymmetricRC_SimpleCycle(t *testing.T) {
	ctx := NewSymmetricContext()

	ctx.EnterScope()

	// Create A and B
	a := ctx.Alloc("A")
	b := ctx.Alloc("B")

	// Create cycle: A → B → A
	ctx.Link(a, b) // A references B
	ctx.Link(b, a) // B references A

	// Both have external_rc=1, internal_rc=1
	if a.ExternalRC != 1 || a.InternalRC != 1 {
		t.Errorf("A: expected ext=1, int=1, got ext=%d, int=%d", a.ExternalRC, a.InternalRC)
	}
	if b.ExternalRC != 1 || b.InternalRC != 1 {
		t.Errorf("B: expected ext=1, int=1, got ext=%d, int=%d", b.ExternalRC, b.InternalRC)
	}

	// Exit scope - should free both despite cycle
	ctx.ExitScope()

	if !a.Freed {
		t.Error("A should be freed after scope exit (cycle collected)")
	}
	if !b.Freed {
		t.Error("B should be freed after scope exit (cycle collected)")
	}
}

func TestSymmetricRC_TriangleCycle(t *testing.T) {
	ctx := NewSymmetricContext()

	ctx.EnterScope()

	// Create triangle: A → B → C → A
	a := ctx.Alloc("A")
	b := ctx.Alloc("B")
	c := ctx.Alloc("C")

	ctx.Link(a, b)
	ctx.Link(b, c)
	ctx.Link(c, a)

	ctx.ExitScope()

	if !a.Freed || !b.Freed || !c.Freed {
		t.Error("All objects in triangle cycle should be freed")
	}
}

func TestSymmetricRC_PartialCycleRelease(t *testing.T) {
	ctx := NewSymmetricContext()

	// Outer scope owns A
	a := ctx.Alloc("A")

	ctx.EnterScope()

	// Inner scope owns B
	b := ctx.Alloc("B")

	// Create cycle: A → B → A
	ctx.Link(a, b)
	ctx.Link(b, a)

	// Exit inner scope - only B loses external ref
	ctx.ExitScope()

	// A still has external ref from outer scope
	if a.Freed {
		t.Error("A should NOT be freed - still owned by outer scope")
	}

	// B lost external ref, but has internal ref from A
	// Since A is still alive, B is reachable - but in symmetric RC,
	// B is orphaned because its external_rc=0
	// This is the key insight: B is garbage even though A refs it
	if !b.Freed {
		t.Error("B should be freed - no external refs")
	}
}

func TestSymmetricRC_ChainNoLeak(t *testing.T) {
	ctx := NewSymmetricContext()

	ctx.EnterScope()

	// Create chain: A → B → C → D
	a := ctx.Alloc("A")
	b := ctx.Alloc("B")
	c := ctx.Alloc("C")
	d := ctx.Alloc("D")

	ctx.Link(a, b)
	ctx.Link(b, c)
	ctx.Link(c, d)

	ctx.ExitScope()

	// All should be freed (no cycles, just chain)
	if !a.Freed || !b.Freed || !c.Freed || !d.Freed {
		t.Error("All objects in chain should be freed")
	}
}

func TestSymmetricRC_NestedScopes(t *testing.T) {
	ctx := NewSymmetricContext()

	ctx.EnterScope() // Scope 1
	a := ctx.Alloc("A")

	ctx.EnterScope() // Scope 2
	b := ctx.Alloc("B")

	ctx.EnterScope() // Scope 3
	c := ctx.Alloc("C")

	ctx.ExitScope() // Exit Scope 3
	if !c.Freed {
		t.Error("C should be freed")
	}
	if a.Freed || b.Freed {
		t.Error("A and B should NOT be freed yet")
	}

	ctx.ExitScope() // Exit Scope 2
	if !b.Freed {
		t.Error("B should be freed")
	}
	if a.Freed {
		t.Error("A should NOT be freed yet")
	}

	ctx.ExitScope() // Exit Scope 1
	if !a.Freed {
		t.Error("A should be freed")
	}
}

func TestSymmetricRC_ComplexCycle(t *testing.T) {
	ctx := NewSymmetricContext()

	ctx.EnterScope()

	// Create doubly-linked list: A ↔ B ↔ C
	a := ctx.Alloc("A")
	b := ctx.Alloc("B")
	c := ctx.Alloc("C")

	// Forward links
	ctx.Link(a, b)
	ctx.Link(b, c)

	// Back links (creating cycles)
	ctx.Link(b, a)
	ctx.Link(c, b)

	ctx.ExitScope()

	if !a.Freed || !b.Freed || !c.Freed {
		t.Error("Doubly-linked list should be fully freed")
	}
}

func TestSymmetricRC_Stats(t *testing.T) {
	ctx := NewSymmetricContext()

	ctx.EnterScope()

	a := ctx.Alloc("A")
	b := ctx.Alloc("B")
	ctx.Link(a, b)
	ctx.Link(b, a)

	ctx.ExitScope()

	stats := ctx.GetStats()

	if stats.ObjectsCreated != 2 {
		t.Errorf("Expected 2 objects created, got %d", stats.ObjectsCreated)
	}
	if stats.ExternalIncRefs != 2 {
		t.Errorf("Expected 2 external inc_refs, got %d", stats.ExternalIncRefs)
	}
	if stats.InternalIncRefs != 2 {
		t.Errorf("Expected 2 internal inc_refs, got %d", stats.InternalIncRefs)
	}

	t.Logf("Stats: created=%d, ext_inc=%d, int_inc=%d, cycles=%d",
		stats.ObjectsCreated, stats.ExternalIncRefs, stats.InternalIncRefs, stats.CyclesCollected)
}

func TestSymmetricRC_SelfLoop(t *testing.T) {
	ctx := NewSymmetricContext()

	ctx.EnterScope()

	// Self-referential object
	a := ctx.Alloc("A")
	ctx.Link(a, a)

	if a.InternalRC != 1 {
		t.Errorf("Expected internal_rc=1 for self-loop, got %d", a.InternalRC)
	}

	ctx.ExitScope()

	if !a.Freed {
		t.Error("Self-referential object should be freed")
	}
}

func TestSymmetricRC_DiamondPattern(t *testing.T) {
	ctx := NewSymmetricContext()

	ctx.EnterScope()

	// Diamond: A → B, A → C, B → D, C → D
	a := ctx.Alloc("A")
	b := ctx.Alloc("B")
	c := ctx.Alloc("C")
	d := ctx.Alloc("D")

	ctx.Link(a, b)
	ctx.Link(a, c)
	ctx.Link(b, d)
	ctx.Link(c, d)

	// D has internal_rc=2 (from B and C)
	if d.InternalRC != 2 {
		t.Errorf("D should have internal_rc=2, got %d", d.InternalRC)
	}

	ctx.ExitScope()

	if !a.Freed || !b.Freed || !c.Freed || !d.Freed {
		t.Error("Diamond pattern should be fully freed")
	}
}

// Benchmark to compare with arena
func BenchmarkSymmetricRC_CycleCreationAndFree(b *testing.B) {
	for i := 0; i < b.N; i++ {
		ctx := NewSymmetricContext()
		ctx.EnterScope()

		// Create 100-node cycle
		nodes := make([]*SymmetricObj, 100)
		for j := 0; j < 100; j++ {
			nodes[j] = ctx.Alloc(j)
		}
		for j := 0; j < 100; j++ {
			ctx.Link(nodes[j], nodes[(j+1)%100])
		}

		ctx.ExitScope()
	}
}
