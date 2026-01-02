package codegen

import (
	"fmt"
	"strings"
)

// OptimizationStats tracks statistics for all optimizations
type OptimizationStats struct {
	// Liveness analysis
	LivenessEarlyFrees int // Variables freed before scope exit
	LivenessUnused     int // Variables that were never used

	// Stack allocation
	StackAllocations int // Primitives allocated on stack
	HeapAllocations  int // Primitives allocated on heap

	// BorrowRef for borrowed references
	BorrowRefCreated  int // BorrowRefs created for borrowed references
	BorrowRefReleased int // BorrowRefs released at scope exit

	// Purity analysis (Vale-style zero-cost access)
	PurityPureExprs    int // Expressions proven pure
	PurityReadOnlyVars int // Variables marked read-only in pure context
	PurityChecksSkipped int // Safety checks skipped due to purity

	// Scope tethering (Vale-style)
	TetheredVars      int // Variables tethered in scope
	TetheredAccesses  int // Accesses that used tethered fast-path
	TetheredGenSkipped int // Generation checks skipped via tethering

	// Ownership-based decisions
	OwnerLocalFrees      int // Locally owned variables freed
	OwnerBorrowedSkips   int // Borrowed references not freed
	OwnerTransferredSkips int // Transferred ownership not freed
	OwnerWeakSkips       int // Weak references not freed

	// RC optimization
	RCIncElided      int // inc_ref calls eliminated
	RCDecElided      int // dec_ref calls eliminated
	RCUniqueDirectFree int // Unique values directly freed

	// Region-based deallocation
	RegionsCreated int // Regions created
	RegionsFlushed int // Regions bulk-deallocated

	// Shape-based free strategy
	ShapeTreeFrees  int // Tree-shaped objects freed
	ShapeDAGFrees   int // DAG-shaped objects freed
	ShapeCyclicArena int // Cyclic objects using arena

	// Reuse analysis
	MemoryReused int // Allocations that reused freed memory

	// Pool allocation
	PoolAllocations int // Allocations in thread-local pool
	PoolEligible    int // Variables eligible for pool allocation
	PoolEscaped     int // Pool values that had to escape to heap

	// Escape analysis
	EscapeNone    int // Non-escaping values
	EscapeArg     int // Values escaping via argument
	EscapeGlobal  int // Values escaping globally
}

// NewOptimizationStats creates a new statistics tracker
func NewOptimizationStats() *OptimizationStats {
	return &OptimizationStats{}
}

// TotalSavings returns total memory operations saved
func (s *OptimizationStats) TotalSavings() int {
	return s.LivenessEarlyFrees + s.StackAllocations + s.BorrowRefCreated +
		s.OwnerBorrowedSkips + s.OwnerTransferredSkips + s.OwnerWeakSkips +
		s.RCIncElided + s.RCDecElided + s.MemoryReused + s.PoolAllocations +
		s.PurityChecksSkipped + s.TetheredGenSkipped
}

// String returns a formatted statistics report
func (s *OptimizationStats) String() string {
	var sb strings.Builder

	sb.WriteString("=== Optimization Statistics ===\n\n")

	sb.WriteString("Liveness Analysis:\n")
	sb.WriteString(fmt.Sprintf("  Early frees:         %d\n", s.LivenessEarlyFrees))
	sb.WriteString(fmt.Sprintf("  Unused variables:    %d\n", s.LivenessUnused))

	sb.WriteString("\nStack Allocation:\n")
	sb.WriteString(fmt.Sprintf("  Stack allocated:     %d\n", s.StackAllocations))
	sb.WriteString(fmt.Sprintf("  Heap allocated:      %d\n", s.HeapAllocations))
	if s.StackAllocations+s.HeapAllocations > 0 {
		pct := float64(s.StackAllocations) / float64(s.StackAllocations+s.HeapAllocations) * 100
		sb.WriteString(fmt.Sprintf("  Stack ratio:         %.1f%%\n", pct))
	}

	sb.WriteString("\nBorrowRef (Borrowed References):\n")
	sb.WriteString(fmt.Sprintf("  BorrowRefs created:     %d\n", s.BorrowRefCreated))
	sb.WriteString(fmt.Sprintf("  BorrowRefs released:    %d\n", s.BorrowRefReleased))

	sb.WriteString("\nPurity Analysis (Vale-style):\n")
	sb.WriteString(fmt.Sprintf("  Pure expressions:    %d\n", s.PurityPureExprs))
	sb.WriteString(fmt.Sprintf("  Read-only vars:      %d\n", s.PurityReadOnlyVars))
	sb.WriteString(fmt.Sprintf("  Checks skipped:      %d\n", s.PurityChecksSkipped))

	sb.WriteString("\nScope Tethering (Vale-style):\n")
	sb.WriteString(fmt.Sprintf("  Tethered vars:       %d\n", s.TetheredVars))
	sb.WriteString(fmt.Sprintf("  Tethered accesses:   %d\n", s.TetheredAccesses))
	sb.WriteString(fmt.Sprintf("  Gen checks skipped:  %d\n", s.TetheredGenSkipped))

	sb.WriteString("\nOwnership-Based Decisions:\n")
	sb.WriteString(fmt.Sprintf("  Local frees:         %d\n", s.OwnerLocalFrees))
	sb.WriteString(fmt.Sprintf("  Borrowed skips:      %d\n", s.OwnerBorrowedSkips))
	sb.WriteString(fmt.Sprintf("  Transferred skips:   %d\n", s.OwnerTransferredSkips))
	sb.WriteString(fmt.Sprintf("  Weak skips:          %d\n", s.OwnerWeakSkips))

	sb.WriteString("\nReference Counting Optimization:\n")
	sb.WriteString(fmt.Sprintf("  inc_ref elided:      %d\n", s.RCIncElided))
	sb.WriteString(fmt.Sprintf("  dec_ref elided:      %d\n", s.RCDecElided))
	sb.WriteString(fmt.Sprintf("  Unique direct free:  %d\n", s.RCUniqueDirectFree))

	sb.WriteString("\nRegion-Based Deallocation:\n")
	sb.WriteString(fmt.Sprintf("  Regions created:     %d\n", s.RegionsCreated))
	sb.WriteString(fmt.Sprintf("  Regions flushed:     %d\n", s.RegionsFlushed))

	sb.WriteString("\nShape-Based Free Strategy:\n")
	sb.WriteString(fmt.Sprintf("  Tree frees:          %d\n", s.ShapeTreeFrees))
	sb.WriteString(fmt.Sprintf("  DAG frees:           %d\n", s.ShapeDAGFrees))
	sb.WriteString(fmt.Sprintf("  Cyclic (arena):      %d\n", s.ShapeCyclicArena))

	sb.WriteString("\nMemory Reuse (Perceus):\n")
	sb.WriteString(fmt.Sprintf("  Allocations reused:  %d\n", s.MemoryReused))

	sb.WriteString("\nPool Allocation:\n")
	sb.WriteString(fmt.Sprintf("  Pool allocations:    %d\n", s.PoolAllocations))
	sb.WriteString(fmt.Sprintf("  Pool eligible:       %d\n", s.PoolEligible))
	sb.WriteString(fmt.Sprintf("  Pool escaped:        %d\n", s.PoolEscaped))

	sb.WriteString("\nEscape Analysis:\n")
	sb.WriteString(fmt.Sprintf("  Non-escaping:        %d\n", s.EscapeNone))
	sb.WriteString(fmt.Sprintf("  Escape via arg:      %d\n", s.EscapeArg))
	sb.WriteString(fmt.Sprintf("  Escape globally:     %d\n", s.EscapeGlobal))

	sb.WriteString(fmt.Sprintf("\n=== Total Savings: %d operations ===\n", s.TotalSavings()))

	return sb.String()
}

// Summary returns a one-line summary
func (s *OptimizationStats) Summary() string {
	total := s.TotalSavings()
	if total == 0 {
		return "No optimizations applied"
	}
	puritySavings := s.PurityChecksSkipped + s.TetheredGenSkipped
	return fmt.Sprintf("Optimizations: %d stack allocs, %d RC elided, %d BorrowRefs, %d purity/tethered, %d reused (total: %d saved)",
		s.StackAllocations, s.RCIncElided+s.RCDecElided, s.BorrowRefCreated, puritySavings, s.MemoryReused, total)
}

// Merge combines stats from another OptimizationStats
func (s *OptimizationStats) Merge(other *OptimizationStats) {
	if other == nil {
		return
	}

	s.LivenessEarlyFrees += other.LivenessEarlyFrees
	s.LivenessUnused += other.LivenessUnused
	s.StackAllocations += other.StackAllocations
	s.HeapAllocations += other.HeapAllocations
	s.BorrowRefCreated += other.BorrowRefCreated
	s.BorrowRefReleased += other.BorrowRefReleased
	s.PurityPureExprs += other.PurityPureExprs
	s.PurityReadOnlyVars += other.PurityReadOnlyVars
	s.PurityChecksSkipped += other.PurityChecksSkipped
	s.TetheredVars += other.TetheredVars
	s.TetheredAccesses += other.TetheredAccesses
	s.TetheredGenSkipped += other.TetheredGenSkipped
	s.OwnerLocalFrees += other.OwnerLocalFrees
	s.OwnerBorrowedSkips += other.OwnerBorrowedSkips
	s.OwnerTransferredSkips += other.OwnerTransferredSkips
	s.OwnerWeakSkips += other.OwnerWeakSkips
	s.RCIncElided += other.RCIncElided
	s.RCDecElided += other.RCDecElided
	s.RCUniqueDirectFree += other.RCUniqueDirectFree
	s.RegionsCreated += other.RegionsCreated
	s.RegionsFlushed += other.RegionsFlushed
	s.ShapeTreeFrees += other.ShapeTreeFrees
	s.ShapeDAGFrees += other.ShapeDAGFrees
	s.ShapeCyclicArena += other.ShapeCyclicArena
	s.MemoryReused += other.MemoryReused
	s.PoolAllocations += other.PoolAllocations
	s.PoolEligible += other.PoolEligible
	s.PoolEscaped += other.PoolEscaped
	s.EscapeNone += other.EscapeNone
	s.EscapeArg += other.EscapeArg
	s.EscapeGlobal += other.EscapeGlobal
}
