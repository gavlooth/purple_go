# Purple Go Implementation Roadmap

## Complete Step-by-Step Plan

This document provides actionable steps to implement all future optimizations and validation testing.

---

# Phase 1: Critical Validation (Week 1-2)

## Step 1.1: Valgrind Integration

**Goal**: Verify no memory leaks in generated C code.

### 1.1.1 Create test harness
```bash
# Create directory
mkdir -p test/validation
```

**File**: `test/validation/valgrind_test.go`
```go
package validation

import (
    "os"
    "os/exec"
    "path/filepath"
    "testing"

    "purple_go/pkg/codegen"
    "purple_go/pkg/parser"
)

func TestValgrindNoLeaks(t *testing.T) {
    tests := []struct {
        name string
        code string
    }{
        {"simple_int", "(+ 1 2)"},
        {"cons_cell", "(let ((x (cons 1 2))) (car x))"},
        {"nested_let", "(let ((x 1)) (let ((y 2)) (+ x y)))"},
        {"lambda", "((lambda (x) (+ x 1)) 5)"},
        {"list_ops", "(fold + 0 (list 1 2 3 4 5))"},
    }

    for _, tc := range tests {
        t.Run(tc.name, func(t *testing.T) {
            // 1. Parse
            expr, err := parser.Parse(tc.code)
            if err != nil {
                t.Fatalf("parse error: %v", err)
            }

            // 2. Generate C
            cCode := codegen.GenerateProgram(expr)

            // 3. Write to temp file
            tmpDir := t.TempDir()
            cFile := filepath.Join(tmpDir, "test.c")
            binFile := filepath.Join(tmpDir, "test")

            if err := os.WriteFile(cFile, []byte(cCode), 0644); err != nil {
                t.Fatalf("write error: %v", err)
            }

            // 4. Compile
            compile := exec.Command("gcc", "-std=c99", "-pthread", "-o", binFile, cFile)
            if out, err := compile.CombinedOutput(); err != nil {
                t.Fatalf("compile error: %v\n%s", err, out)
            }

            // 5. Run with Valgrind
            valgrind := exec.Command("valgrind",
                "--leak-check=full",
                "--error-exitcode=1",
                "--errors-for-leak-kinds=all",
                binFile)
            if out, err := valgrind.CombinedOutput(); err != nil {
                t.Errorf("valgrind found issues:\n%s", out)
            }
        })
    }
}
```

### 1.1.2 Create comprehensive test cases

**File**: `test/validation/testcases.go`
```go
package validation

var MemoryTestCases = []struct {
    Name     string
    Code     string
    Expected string
}{
    // Basic allocations
    {"int_alloc", "(+ 1 2)", "3"},
    {"pair_alloc", "(car (cons 1 2))", "1"},
    {"nested_pairs", "(car (cdr (cons 1 (cons 2 3))))", "2"},

    // Let bindings
    {"let_simple", "(let ((x 1)) x)", "1"},
    {"let_nested", "(let ((x 1)) (let ((y 2)) (+ x y)))", "3"},
    {"let_shadow", "(let ((x 1)) (let ((x 2)) x))", "2"},

    // Functions
    {"lambda_simple", "((lambda (x) x) 42)", "42"},
    {"lambda_closure", "(let ((y 10)) ((lambda (x) (+ x y)) 5))", "15"},
    {"recursive", "(letrec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 5))", "120"},

    // Lists
    {"list_create", "(length (list 1 2 3))", "3"},
    {"list_map", "(car (map (lambda (x) (+ x 1)) (list 1 2 3)))", "2"},
    {"list_fold", "(fold + 0 (list 1 2 3 4 5))", "15"},

    // Boxes (mutable)
    {"box_simple", "(let ((b (box 1))) (unbox b))", "1"},
    {"box_set", "(let ((b (box 1))) (do (set-box! b 2) (unbox b)))", "2"},

    // Error handling
    {"try_no_error", "(try (+ 1 2) (lambda (e) 0))", "3"},
    {"try_with_error", "(try (error \"fail\") (lambda (e) 42))", "42"},

    // User-defined types
    {"deftype_simple", "(do (deftype Point (x int) (y int)) (Point-x (mk-Point 3 4)))", "3"},

    // Cycles with weak refs
    {"weak_cycle", `
        (do
          (deftype Node (val int) (next Node) (prev Node :weak))
          (let ((a (mk-Node 1 nil nil))
                (b (mk-Node 2 nil nil)))
            (do
              (set! (Node-next a) b)
              (set! (Node-prev b) a)
              (Node-val a))))
    `, "1"},
}
```

### 1.1.3 Run initial validation
```bash
go test ./test/validation/... -v -run TestValgrind
```

---

## Step 1.2: AddressSanitizer Integration

**Goal**: Detect buffer overflows, use-after-free, double-free.

### 1.2.1 Create ASan test

**File**: `test/validation/asan_test.go`
```go
package validation

import (
    "os"
    "os/exec"
    "path/filepath"
    "testing"

    "purple_go/pkg/codegen"
    "purple_go/pkg/parser"
)

func TestAddressSanitizer(t *testing.T) {
    for _, tc := range MemoryTestCases {
        t.Run(tc.Name, func(t *testing.T) {
            expr, _ := parser.Parse(tc.Code)
            cCode := codegen.GenerateProgram(expr)

            tmpDir := t.TempDir()
            cFile := filepath.Join(tmpDir, "test.c")
            binFile := filepath.Join(tmpDir, "test")

            os.WriteFile(cFile, []byte(cCode), 0644)

            // Compile with ASan
            compile := exec.Command("gcc",
                "-std=c99", "-pthread",
                "-fsanitize=address",
                "-fno-omit-frame-pointer",
                "-g",
                "-o", binFile, cFile)
            if out, err := compile.CombinedOutput(); err != nil {
                t.Fatalf("compile error: %v\n%s", err, out)
            }

            // Run (ASan will abort on error)
            run := exec.Command(binFile)
            run.Env = append(os.Environ(), "ASAN_OPTIONS=detect_leaks=1")
            if out, err := run.CombinedOutput(); err != nil {
                t.Errorf("ASan detected issue:\n%s", out)
            }
        })
    }
}
```

---

## Step 1.3: ThreadSanitizer Integration

**Goal**: Detect data races in concurrent code.

### 1.3.1 Create TSan test

**File**: `test/validation/tsan_test.go`
```go
package validation

var ConcurrencyTestCases = []struct {
    Name string
    Code string
}{
    {"channel_transfer", `
        (let ((ch (make-chan 1))
              (x (cons 1 2)))
          (do
            (go (lambda () (chan-send! ch x)))
            (chan-recv! ch)))
    `},
    {"shared_read", `
        (let ((x (cons 1 2)))
          (do
            (go (lambda () (car x)))
            (go (lambda () (cdr x)))
            x))
    `},
    {"producer_consumer", `
        (let ((ch (make-chan 10)))
          (do
            (go (lambda ()
                  (do (chan-send! ch 1)
                      (chan-send! ch 2)
                      (chan-send! ch 3))))
            (+ (chan-recv! ch)
               (chan-recv! ch)
               (chan-recv! ch))))
    `},
}

func TestThreadSanitizer(t *testing.T) {
    for _, tc := range ConcurrencyTestCases {
        t.Run(tc.Name, func(t *testing.T) {
            expr, _ := parser.Parse(tc.Code)
            cCode := codegen.GenerateProgram(expr)

            tmpDir := t.TempDir()
            cFile := filepath.Join(tmpDir, "test.c")
            binFile := filepath.Join(tmpDir, "test")

            os.WriteFile(cFile, []byte(cCode), 0644)

            // Compile with TSan
            compile := exec.Command("gcc",
                "-std=c99", "-pthread",
                "-fsanitize=thread",
                "-g",
                "-o", binFile, cFile)
            if out, err := compile.CombinedOutput(); err != nil {
                t.Fatalf("compile error: %v\n%s", err, out)
            }

            // Run
            run := exec.Command(binFile)
            if out, err := run.CombinedOutput(); err != nil {
                t.Errorf("TSan detected race:\n%s", out)
            }
        })
    }
}
```

---

## Step 1.4: Correctness Testing

**Goal**: Compiled code produces same results as interpreter.

### 1.4.1 Create comparison harness

**File**: `test/validation/correctness_test.go`
```go
package validation

import (
    "testing"

    "purple_go/pkg/eval"
    "purple_go/pkg/jit"
    "purple_go/pkg/parser"
)

func TestCompiledMatchesInterpreter(t *testing.T) {
    for _, tc := range MemoryTestCases {
        t.Run(tc.Name, func(t *testing.T) {
            expr, err := parser.Parse(tc.Code)
            if err != nil {
                t.Fatalf("parse error: %v", err)
            }

            // Run in interpreter
            interpResult, err := eval.Eval(expr, eval.NewEnv())
            if err != nil {
                t.Fatalf("interp error: %v", err)
            }

            // Run compiled
            jitResult, err := jit.CompileAndRun(expr)
            if err != nil {
                t.Fatalf("jit error: %v", err)
            }

            // Compare
            if !eval.Equal(interpResult, jitResult) {
                t.Errorf("mismatch: interp=%v, compiled=%v",
                    eval.Show(interpResult), eval.Show(jitResult))
            }
        })
    }
}
```

---

# Phase 2: RC Elimination (Week 3-4)

## Step 2.1: Enhance Uniqueness Analysis

**Goal**: Identify more cases where RC can be skipped.

### 2.1.1 Add uniqueness propagation

**File**: `pkg/analysis/rcopt.go` (extend)
```go
// Add to RCOptContext

// PropagateUniqueness propagates uniqueness through expressions
func (ctx *RCOptContext) PropagateUniqueness(expr *ast.Value) {
    if expr == nil || ast.IsNil(expr) {
        return
    }

    switch expr.Tag {
    case ast.TCell:
        if ast.IsSym(expr.Car) {
            switch expr.Car.Str {
            case "let":
                ctx.propagateLetUniqueness(expr)
            case "if":
                ctx.propagateIfUniqueness(expr)
            case "lambda":
                ctx.propagateLambdaUniqueness(expr)
            }
        }
    }
}

func (ctx *RCOptContext) propagateLetUniqueness(expr *ast.Value) {
    // For each binding:
    // - If RHS is a fresh allocation, var is unique
    // - If RHS is a unique var and var goes out of scope, transfer uniqueness
    bindings := expr.Cdr.Car
    for !ast.IsNil(bindings) && ast.IsCell(bindings) {
        binding := bindings.Car
        if ast.IsCell(binding) && ast.IsSym(binding.Car) {
            varName := binding.Car.Str
            initExpr := binding.Cdr.Car

            if ctx.isFreshAllocation(initExpr) {
                ctx.MarkUnique(varName)
            } else if ast.IsSym(initExpr) {
                sourceVar := initExpr.Str
                if ctx.IsUnique(sourceVar) {
                    // Transfer uniqueness (source becomes non-unique)
                    ctx.TransferUniqueness(sourceVar, varName)
                }
            }
        }
        bindings = bindings.Cdr
    }
}

func (ctx *RCOptContext) isFreshAllocation(expr *ast.Value) bool {
    if !ast.IsCell(expr) {
        return false
    }
    if ast.IsSym(expr.Car) {
        switch expr.Car.Str {
        case "cons", "list", "box", "mk-int", "mk-pair", "lambda":
            return true
        }
        // Check for user-defined constructors (mk-*)
        if len(expr.Car.Str) > 3 && expr.Car.Str[:3] == "mk-" {
            return true
        }
    }
    return false
}

// TransferUniqueness transfers uniqueness from source to dest
func (ctx *RCOptContext) TransferUniqueness(source, dest string) {
    if info, ok := ctx.Vars[source]; ok && info.IsUnique {
        info.IsUnique = false
        ctx.Vars[dest] = &VarRCInfo{IsUnique: true}
    }
}
```

### 2.1.2 Add borrowed reference tracking

```go
// Add to RCOptContext

// BorrowedRef tracks a borrowed reference
type BorrowedRef struct {
    Source    string // Variable borrowed from
    Field     string // Field accessed (empty for whole value)
    ValidUntil string // Scope where borrow ends
}

// Borrows maps borrowed vars to their source
Borrows map[string]*BorrowedRef

// MarkBorrowed marks a variable as borrowed from another
func (ctx *RCOptContext) MarkBorrowed(borrowed, source, field string) {
    ctx.Borrows[borrowed] = &BorrowedRef{
        Source: source,
        Field:  field,
    }
}

// IsBorrowed returns true if var is borrowed (no RC needed)
func (ctx *RCOptContext) IsBorrowed(name string) bool {
    _, ok := ctx.Borrows[name]
    return ok
}
```

### 2.1.3 Generate optimized RC operations

**File**: `pkg/codegen/codegen.go` (extend GenerateLet)
```go
// In GenerateLet, modify the free generation:

// Generate frees based on analysis
for i := len(bindings) - 1; i >= 0; i-- {
    bi := bindings[i]
    varName := bi.sym.Str

    // Check RC optimization
    if g.enableRCOpt {
        // Skip if unique (will be freed directly)
        if g.rcOptCtx.IsUnique(varName) {
            sb.WriteString(fmt.Sprintf("    free_unique(%s);\n", varName))
            continue
        }

        // Skip if borrowed (source handles RC)
        if g.rcOptCtx.IsBorrowed(varName) {
            sb.WriteString(fmt.Sprintf("    /* %s is borrowed, no RC */\n", varName))
            continue
        }

        // Skip if transferred (new owner handles)
        if g.IsTransferred(varName) {
            sb.WriteString(fmt.Sprintf("    /* %s transferred, no RC */\n", varName))
            continue
        }
    }

    // Default: use dec_ref
    sb.WriteString(fmt.Sprintf("    %s;\n", g.GenerateRCOperation(varName, "dec")))
}
```

### 2.1.4 Add RC elimination statistics

```go
// Add to RCOptContext

type RCStats struct {
    TotalOps     int
    EliminatedOps int
    UniqueSkips   int
    BorrowSkips   int
    TransferSkips int
}

func (ctx *RCOptContext) GetStats() RCStats {
    return ctx.Stats
}

func (ctx *RCOptContext) ReportStats() string {
    s := ctx.Stats
    pct := 0.0
    if s.TotalOps > 0 {
        pct = float64(s.EliminatedOps) / float64(s.TotalOps) * 100
    }
    return fmt.Sprintf("RC ops: %d total, %d eliminated (%.1f%%) [unique:%d, borrow:%d, transfer:%d]",
        s.TotalOps, s.EliminatedOps, pct,
        s.UniqueSkips, s.BorrowSkips, s.TransferSkips)
}
```

### 2.1.5 Test RC elimination

**File**: `pkg/analysis/rcopt_elimination_test.go`
```go
func TestRCElimination(t *testing.T) {
    tests := []struct {
        name     string
        code     string
        minElim  float64 // Minimum elimination percentage
    }{
        {"unique_int", "(let ((x (+ 1 2))) x)", 100.0},
        {"unique_pair", "(let ((x (cons 1 2))) (car x))", 50.0},
        {"borrowed_car", "(let ((x (cons 1 2))) (let ((y (car x))) y))", 50.0},
        {"chain", "(let ((a 1)) (let ((b a)) (let ((c b)) c)))", 66.0},
    }

    for _, tc := range tests {
        t.Run(tc.name, func(t *testing.T) {
            ctx := NewRCOptContext()
            expr, _ := parser.Parse(tc.code)
            ctx.Analyze(expr)

            stats := ctx.GetStats()
            if stats.TotalOps == 0 {
                return
            }

            pct := float64(stats.EliminatedOps) / float64(stats.TotalOps) * 100
            if pct < tc.minElim {
                t.Errorf("elimination %.1f%% < expected %.1f%%", pct, tc.minElim)
            }
            t.Logf("%s: %s", tc.name, ctx.ReportStats())
        })
    }
}
```

---

# Phase 3: Active Reuse (Week 5-6)

## Step 3.1: Integrate Reuse into Let Generation

**Goal**: Transform `free(x); y = alloc()` → `y = reuse(x)`.

### 3.1.1 Track allocations with types

**File**: `pkg/analysis/reuse.go` (extend)
```go
// AllocationInfo tracks info about an allocation
type AllocationInfo struct {
    VarName   string
    TypeName  string
    Size      int
    Line      int
    IsFresh   bool
    IsFreed   bool
    FreeLine  int
}

// Add to ReuseContext
Allocations map[string]*AllocationInfo

// RegisterAllocation records an allocation
func (ctx *ReuseContext) RegisterAllocation(varName, typeName string, line int) {
    ctx.Allocations[varName] = &AllocationInfo{
        VarName:  varName,
        TypeName: typeName,
        Size:     ctx.TypeSizes.GetSize(typeName),
        Line:     line,
        IsFresh:  true,
    }
}

// MarkFreed marks an allocation as freed (available for reuse)
func (ctx *ReuseContext) MarkFreed(varName string, line int) {
    if info, ok := ctx.Allocations[varName]; ok {
        info.IsFreed = true
        info.FreeLine = line
        ctx.AddPendingFree(varName, info.TypeName)
    }
}

// FindBestReuse finds the best reuse candidate for a new allocation
func (ctx *ReuseContext) FindBestReuse(typeName string, line int) *ReuseCandidate {
    targetSize := ctx.TypeSizes.GetSize(typeName)

    var best *ReuseCandidate
    bestWaste := int(^uint(0) >> 1) // Max int

    for i := len(ctx.PendingFrees) - 1; i >= 0; i-- {
        freeVar := ctx.PendingFrees[i]
        freeType := ctx.PendingTypes[freeVar]
        freeSize := ctx.TypeSizes.GetSize(freeType)

        if freeSize >= targetSize {
            waste := freeSize - targetSize
            if waste < bestWaste {
                best = &ReuseCandidate{
                    FreeVar:   freeVar,
                    FreeType:  freeType,
                    AllocType: typeName,
                    CanReuse:  true,
                }
                bestWaste = waste

                if waste == 0 {
                    break // Perfect match
                }
            }
        }
    }

    return best
}
```

### 3.1.2 Modify GenerateLet to use reuse

**File**: `pkg/codegen/codegen.go` (modify GenerateLet)
```go
func (g *CodeGenerator) GenerateLet(bindings []struct {
    sym *ast.Value
    val *ast.Value
}, body *ast.Value) string {
    var sb strings.Builder
    sb.WriteString("({\n")

    // Phase 1: Analyze all bindings for reuse opportunities
    for _, bi := range bindings {
        g.reuseCtx.Ctx.RegisterAllocation(bi.sym.Str, g.inferType(bi.val), 0)
    }

    // Phase 2: Generate declarations with potential reuse
    for _, bi := range bindings {
        varName := bi.sym.Str
        valStr := g.ValueToCExpr(bi.val)
        typeName := g.inferType(bi.val)

        // Check for reuse opportunity
        if candidate := g.reuseCtx.Ctx.FindBestReuse(typeName, 0); candidate != nil {
            // Remove from pending
            g.reuseCtx.Ctx.ConsumePendingFree(candidate.FreeVar)

            // Generate reuse
            sb.WriteString(fmt.Sprintf("    /* Reuse %s for %s */\n",
                candidate.FreeVar, varName))
            sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n",
                varName, g.generateReuseExpr(candidate, valStr)))
        } else {
            // Normal allocation
            sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", varName, valStr))
        }
    }

    // Phase 3: Generate body
    bodyStr := g.ValueToCExpr(body)
    sb.WriteString(fmt.Sprintf("    Obj* _res = %s;\n", bodyStr))

    // Phase 4: Generate frees (remaining allocations become pending for outer scope)
    for i := len(bindings) - 1; i >= 0; i-- {
        bi := bindings[i]
        varName := bi.sym.Str
        typeName := g.inferType(bi.val)

        // Mark as freed (available for reuse in outer scope)
        g.reuseCtx.Ctx.MarkFreed(varName, 0)

        // Generate free (or it will be reused)
        if !g.reuseCtx.Ctx.WillBeReused(varName) {
            sb.WriteString(fmt.Sprintf("    dec_ref(%s);\n", varName))
        }
    }

    sb.WriteString("    _res;\n")
    sb.WriteString("})")

    return sb.String()
}

func (g *CodeGenerator) generateReuseExpr(candidate *ReuseCandidate, initExpr string) string {
    switch candidate.AllocType {
    case "int":
        return fmt.Sprintf("reuse_as_int(%s, %s)", candidate.FreeVar, initExpr)
    case "pair":
        return fmt.Sprintf("reuse_as_pair(%s, %s)", candidate.FreeVar, initExpr)
    case "box":
        return fmt.Sprintf("reuse_as_box(%s, %s)", candidate.FreeVar, initExpr)
    default:
        return initExpr // Fall back to normal allocation
    }
}

func (g *CodeGenerator) inferType(val *ast.Value) string {
    if val == nil || ast.IsNil(val) {
        return "Obj"
    }
    if ast.IsInt(val) {
        return "int"
    }
    if ast.IsCell(val) && ast.IsSym(val.Car) {
        switch val.Car.Str {
        case "cons", "list":
            return "pair"
        case "box":
            return "box"
        case "lambda":
            return "closure"
        }
    }
    return "Obj"
}
```

### 3.1.3 Test reuse transformation

**File**: `pkg/codegen/reuse_codegen_test.go`
```go
func TestReuseCodeGeneration(t *testing.T) {
    tests := []struct {
        name     string
        code     string
        contains string
    }{
        {
            "pair_reuse",
            "(let ((x (cons 1 2))) (let ((y (cons 3 4))) y))",
            "reuse_as_pair",
        },
        {
            "int_reuse",
            "(let ((x (+ 1 2))) (let ((y (+ 3 4))) y))",
            "reuse_as_int",
        },
    }

    for _, tc := range tests {
        t.Run(tc.name, func(t *testing.T) {
            expr, _ := parser.Parse(tc.code)

            var sb strings.Builder
            gen := codegen.NewCodeGenerator(&sb)
            gen.GenerateExpr(expr)

            code := sb.String()
            if !strings.Contains(code, tc.contains) {
                t.Errorf("expected %q in generated code:\n%s", tc.contains, code)
            }
        })
    }
}
```

---

# Phase 4: Interprocedural Ownership (Week 7-8)

## Step 4.1: Use Summaries at Call Sites

**Goal**: Optimize RC based on callee ownership behavior.

### 4.1.1 Query summaries during codegen

**File**: `pkg/codegen/codegen.go` (add method)
```go
// GenerateCall generates a function call with ownership-aware RC
func (g *CodeGenerator) GenerateCall(fn string, args []*ast.Value) string {
    var sb strings.Builder

    // Get function summary
    summary := g.summaryCtx.Registry.Lookup(fn)

    // Generate argument expressions
    argExprs := make([]string, len(args))
    for i, arg := range args {
        argExprs[i] = g.ValueToCExpr(arg)
    }

    // Check each argument's ownership
    for i, arg := range args {
        if ast.IsSym(arg) {
            varName := arg.Str

            var ownership analysis.OwnershipClass
            if summary != nil && i < len(summary.Params) {
                ownership = summary.Params[i].Ownership
            } else {
                ownership = analysis.OwnerBorrowed // Default
            }

            switch ownership {
            case analysis.OwnerConsumed:
                // Callee will free - don't inc_ref, mark as transferred
                g.concurrencyCtx.Ctx.MarkTransferred(varName)
                sb.WriteString(fmt.Sprintf("/* %s consumed by %s */\n", varName, fn))

            case analysis.OwnerBorrowed:
                // Callee borrows - may need inc_ref if not unique
                if !g.rcOptCtx.IsUnique(varName) {
                    sb.WriteString(fmt.Sprintf("inc_ref(%s);\n", varName))
                }
            }
        }
    }

    // Generate call
    sb.WriteString(fmt.Sprintf("%s(%s)", fn, strings.Join(argExprs, ", ")))

    return sb.String()
}
```

### 4.1.2 Auto-generate summaries for user functions

**File**: `pkg/eval/eval.go` (extend evalDefine)
```go
// In evalDefine, after registering the function:

// Generate summary for interprocedural analysis
if codegen.GlobalCodeGenerator() != nil {
    codegen.GlobalCodeGenerator().AnalyzeFunction(name, params, body)
}
```

### 4.1.3 Test interprocedural optimization

**File**: `pkg/analysis/interprocedural_test.go`
```go
func TestInterproceduralOwnership(t *testing.T) {
    code := `
        (do
          (define (consume-pair p) (car p))
          (let ((x (cons 1 2)))
            (consume-pair x)))
    `

    expr, _ := parser.Parse(code)

    // Analyze
    sa := analysis.NewSummaryAnalyzer()
    // ... extract define and analyze

    summary := sa.Registry.Lookup("consume-pair")
    if summary == nil {
        t.Fatal("summary not found")
    }

    // Parameter should be borrowed (not consumed)
    if summary.Params[0].Ownership != analysis.OwnerBorrowed {
        t.Errorf("expected borrowed, got %v", summary.Params[0].Ownership)
    }
}
```

---

# Phase 5: DPS Code Generation (Week 9-12)

## Step 5.1: Identify DPS Candidates

**Goal**: Find functions that return fresh allocations.

### 5.1.1 Add DPS analysis

**File**: `pkg/analysis/dps.go` (new file)
```go
package analysis

import "purple_go/pkg/ast"

// DPSCandidate represents a function eligible for DPS transformation
type DPSCandidate struct {
    Name       string
    Params     []string
    ReturnType string
    IsTailCall bool
    BodyExpr   *ast.Value
}

// DPSAnalyzer identifies DPS transformation opportunities
type DPSAnalyzer struct {
    Candidates map[string]*DPSCandidate
    Registry   *SummaryRegistry
}

func NewDPSAnalyzer(registry *SummaryRegistry) *DPSAnalyzer {
    return &DPSAnalyzer{
        Candidates: make(map[string]*DPSCandidate),
        Registry:   registry,
    }
}

// AnalyzeFunction checks if a function is a DPS candidate
func (da *DPSAnalyzer) AnalyzeFunction(name string, params *ast.Value, body *ast.Value) *DPSCandidate {
    summary := da.Registry.Lookup(name)
    if summary == nil {
        return nil
    }

    // Must return fresh allocation
    if summary.Return == nil || !summary.Return.IsFresh {
        return nil
    }

    // Must allocate O(n) or more
    if summary.Allocations == 0 {
        return nil
    }

    // Check if tail-recursive
    isTail := da.isTailRecursive(body, name)

    candidate := &DPSCandidate{
        Name:       name,
        Params:     extractParamNames(params),
        ReturnType: "Obj",
        IsTailCall: isTail,
        BodyExpr:   body,
    }

    da.Candidates[name] = candidate
    return candidate
}

func (da *DPSAnalyzer) isTailRecursive(body *ast.Value, fnName string) bool {
    // Check if the function calls itself in tail position
    return da.isInTailPosition(body, fnName, true)
}

func (da *DPSAnalyzer) isInTailPosition(expr *ast.Value, fnName string, isTail bool) bool {
    if expr == nil || ast.IsNil(expr) {
        return false
    }

    if !ast.IsCell(expr) {
        return false
    }

    if ast.IsSym(expr.Car) {
        switch expr.Car.Str {
        case fnName:
            return isTail
        case "if":
            // Both branches must be checked
            thenBranch := expr.Cdr.Cdr.Car
            elseBranch := expr.Cdr.Cdr.Cdr.Car
            return da.isInTailPosition(thenBranch, fnName, isTail) ||
                   da.isInTailPosition(elseBranch, fnName, isTail)
        case "let", "letrec":
            // Body is in tail position
            bodyExpr := expr.Cdr.Cdr.Car
            return da.isInTailPosition(bodyExpr, fnName, isTail)
        case "do":
            // Last expression is in tail position
            last := expr.Cdr
            for !ast.IsNil(last.Cdr) && ast.IsCell(last.Cdr) {
                last = last.Cdr
            }
            return da.isInTailPosition(last.Car, fnName, isTail)
        }
    }

    return false
}
```

### 5.1.2 Generate DPS variants

**File**: `pkg/codegen/dps_codegen.go` (new file)
```go
package codegen

import (
    "fmt"
    "strings"

    "purple_go/pkg/analysis"
)

// DPSCodeGenerator generates DPS function variants
type DPSCodeGenerator struct {
    analyzer *analysis.DPSAnalyzer
}

func NewDPSCodeGenerator(analyzer *analysis.DPSAnalyzer) *DPSCodeGenerator {
    return &DPSCodeGenerator{analyzer: analyzer}
}

// GenerateDPSVariant generates a DPS version of a function
func (g *DPSCodeGenerator) GenerateDPSVariant(candidate *analysis.DPSCandidate) string {
    var sb strings.Builder

    // Function signature with destination parameter
    params := append([]string{"Obj** _dest"}, candidate.Params...)
    sb.WriteString(fmt.Sprintf("void %s_dps(%s) {\n",
        candidate.Name, strings.Join(params, ", Obj* ")))

    // Generate body with destination passing
    g.generateDPSBody(&sb, candidate)

    sb.WriteString("}\n")
    return sb.String()
}

func (g *DPSCodeGenerator) generateDPSBody(sb *strings.Builder, candidate *analysis.DPSCandidate) {
    // For tail-recursive functions, transform to iterative with destination
    if candidate.IsTailCall {
        g.generateTailRecursiveDPS(sb, candidate)
    } else {
        g.generateSimpleDPS(sb, candidate)
    }
}

func (g *DPSCodeGenerator) generateSimpleDPS(sb *strings.Builder, candidate *analysis.DPSCandidate) {
    // Simple case: just write result to destination
    sb.WriteString("    *_dest = ")
    // ... generate body expression
    sb.WriteString(";\n")
}

func (g *DPSCodeGenerator) generateTailRecursiveDPS(sb *strings.Builder, candidate *analysis.DPSCandidate) {
    // Transform tail recursion to iteration
    sb.WriteString("    _loop:\n")
    // ... generate iterative version
}

// GenerateAllDPSVariants generates DPS variants for all candidates
func (g *DPSCodeGenerator) GenerateAllDPSVariants() string {
    var sb strings.Builder

    sb.WriteString("/* ========== DPS Function Variants ========== */\n\n")

    for _, candidate := range g.analyzer.Candidates {
        sb.WriteString(g.GenerateDPSVariant(candidate))
        sb.WriteString("\n")
    }

    return sb.String()
}
```

---

# Phase 6: Region Inference (Week 13-16)

## Step 6.1: Basic Region Analysis

**Goal**: Group allocations with same lifetime.

### 6.1.1 Create region analysis

**File**: `pkg/analysis/region.go` (new file)
```go
package analysis

import "purple_go/pkg/ast"

// Region represents a memory region
type Region struct {
    ID         int
    Name       string
    Parent     *Region
    Children   []*Region
    Allocations []string  // Variables allocated in this region
    Lifetime   Lifetime
}

// Lifetime describes when a region can be freed
type Lifetime int

const (
    LifetimeScope    Lifetime = iota // Freed at scope exit
    LifetimeReturn                    // Freed when caller done with result
    LifetimeGlobal                    // Never freed (static)
)

// RegionAnalyzer infers regions for allocations
type RegionAnalyzer struct {
    Regions    map[int]*Region
    VarRegion  map[string]int  // Variable -> Region ID
    NextID     int
    CurrentRgn *Region
}

func NewRegionAnalyzer() *RegionAnalyzer {
    ra := &RegionAnalyzer{
        Regions:   make(map[int]*Region),
        VarRegion: make(map[string]int),
    }
    // Create global region
    global := &Region{ID: 0, Name: "global", Lifetime: LifetimeGlobal}
    ra.Regions[0] = global
    ra.CurrentRgn = global
    return ra
}

// EnterScope creates a new region for a scope
func (ra *RegionAnalyzer) EnterScope(name string) *Region {
    rgn := &Region{
        ID:       ra.NextID,
        Name:     name,
        Parent:   ra.CurrentRgn,
        Lifetime: LifetimeScope,
    }
    ra.NextID++
    ra.Regions[rgn.ID] = rgn
    ra.CurrentRgn.Children = append(ra.CurrentRgn.Children, rgn)
    ra.CurrentRgn = rgn
    return rgn
}

// ExitScope returns to parent region
func (ra *RegionAnalyzer) ExitScope() *Region {
    exited := ra.CurrentRgn
    if ra.CurrentRgn.Parent != nil {
        ra.CurrentRgn = ra.CurrentRgn.Parent
    }
    return exited
}

// AllocateIn allocates a variable in the current region
func (ra *RegionAnalyzer) AllocateIn(varName string) {
    ra.CurrentRgn.Allocations = append(ra.CurrentRgn.Allocations, varName)
    ra.VarRegion[varName] = ra.CurrentRgn.ID
}

// Analyze performs region inference on an expression
func (ra *RegionAnalyzer) Analyze(expr *ast.Value) {
    ra.analyzeExpr(expr)
}

func (ra *RegionAnalyzer) analyzeExpr(expr *ast.Value) {
    if expr == nil || ast.IsNil(expr) {
        return
    }

    if !ast.IsCell(expr) {
        return
    }

    if ast.IsSym(expr.Car) {
        switch expr.Car.Str {
        case "let", "letrec":
            ra.analyzeLet(expr)
        case "lambda":
            ra.analyzeLambda(expr)
        case "cons", "list", "box":
            // Allocation in current region
            // (handled at binding site)
        default:
            // Recurse
            for e := expr.Cdr; !ast.IsNil(e) && ast.IsCell(e); e = e.Cdr {
                ra.analyzeExpr(e.Car)
            }
        }
    }
}

func (ra *RegionAnalyzer) analyzeLet(expr *ast.Value) {
    // Create region for let scope
    ra.EnterScope("let")

    // Process bindings
    bindings := expr.Cdr.Car
    for !ast.IsNil(bindings) && ast.IsCell(bindings) {
        binding := bindings.Car
        if ast.IsCell(binding) && ast.IsSym(binding.Car) {
            varName := binding.Car.Str
            ra.AllocateIn(varName)

            // Analyze init expression
            if binding.Cdr != nil && ast.IsCell(binding.Cdr) {
                ra.analyzeExpr(binding.Cdr.Car)
            }
        }
        bindings = bindings.Cdr
    }

    // Analyze body
    if expr.Cdr.Cdr != nil && ast.IsCell(expr.Cdr.Cdr) {
        ra.analyzeExpr(expr.Cdr.Cdr.Car)
    }

    ra.ExitScope()
}

func (ra *RegionAnalyzer) analyzeLambda(expr *ast.Value) {
    // Lambda creates a region for captured variables
    ra.EnterScope("lambda")

    // Analyze body
    if expr.Cdr != nil && expr.Cdr.Cdr != nil {
        ra.analyzeExpr(expr.Cdr.Cdr.Car)
    }

    ra.ExitScope()
}

// GetRegionForVar returns the region a variable is allocated in
func (ra *RegionAnalyzer) GetRegionForVar(varName string) *Region {
    if id, ok := ra.VarRegion[varName]; ok {
        return ra.Regions[id]
    }
    return nil
}

// CanMergeRegions checks if two regions can be merged
func (ra *RegionAnalyzer) CanMergeRegions(r1, r2 *Region) bool {
    // Can merge if same lifetime and one is child of other
    if r1.Lifetime != r2.Lifetime {
        return false
    }
    return r1.Parent == r2 || r2.Parent == r1
}
```

### 6.1.2 Generate region-based allocation

**File**: `pkg/codegen/region_codegen.go`
```go
package codegen

import (
    "fmt"
    "strings"

    "purple_go/pkg/analysis"
)

// GenerateRegionLet generates a let with region allocation
func (g *CodeGenerator) GenerateRegionLet(bindings []struct {
    sym *ast.Value
    val *ast.Value
}, body *ast.Value, region *analysis.Region) string {
    var sb strings.Builder

    // Create region
    sb.WriteString(fmt.Sprintf("Region* _rgn_%d = region_create();\n", region.ID))
    sb.WriteString("({\n")

    // Allocate in region
    for _, bi := range bindings {
        varName := bi.sym.Str
        valStr := g.ValueToCExpr(bi.val)

        // Use region allocation
        sb.WriteString(fmt.Sprintf("    Obj* %s = region_alloc(_rgn_%d, %s);\n",
            varName, region.ID, valStr))
    }

    // Body
    bodyStr := g.ValueToCExpr(body)
    sb.WriteString(fmt.Sprintf("    Obj* _res = %s;\n", bodyStr))

    // Destroy region (frees all allocations)
    sb.WriteString(fmt.Sprintf("    region_destroy(_rgn_%d);\n", region.ID))
    sb.WriteString("    _res;\n")
    sb.WriteString("})")

    return sb.String()
}
```

---

# Phase 7: Performance Benchmarks (Week 17-18)

## Step 7.1: Create Benchmark Suite

**File**: `test/benchmark/benchmark_test.go`
```go
package benchmark

import (
    "testing"

    "purple_go/pkg/eval"
    "purple_go/pkg/jit"
    "purple_go/pkg/parser"
)

func BenchmarkListOperations(b *testing.B) {
    code := "(fold + 0 (map (lambda (x) (* x x)) (range 1000)))"
    expr, _ := parser.Parse(code)

    b.Run("interpreter", func(b *testing.B) {
        for i := 0; i < b.N; i++ {
            eval.Eval(expr, eval.NewEnv())
        }
    })

    b.Run("compiled", func(b *testing.B) {
        for i := 0; i < b.N; i++ {
            jit.CompileAndRun(expr)
        }
    })
}

func BenchmarkRecursion(b *testing.B) {
    code := "(letrec ((fib (lambda (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))) (fib 20))"
    expr, _ := parser.Parse(code)

    b.Run("interpreter", func(b *testing.B) {
        for i := 0; i < b.N; i++ {
            eval.Eval(expr, eval.NewEnv())
        }
    })

    b.Run("compiled", func(b *testing.B) {
        for i := 0; i < b.N; i++ {
            jit.CompileAndRun(expr)
        }
    })
}

func BenchmarkAllocation(b *testing.B) {
    code := `
        (letrec ((build (lambda (n)
                          (if (= n 0)
                              nil
                              (cons n (build (- n 1)))))))
          (build 10000))
    `
    expr, _ := parser.Parse(code)

    b.Run("compiled", func(b *testing.B) {
        for i := 0; i < b.N; i++ {
            jit.CompileAndRun(expr)
        }
    })
}

func BenchmarkCycles(b *testing.B) {
    code := `
        (do
          (deftype Node (val int) (next Node) (prev Node :weak))
          (letrec ((build (lambda (n prev)
                            (if (= n 0)
                                nil
                                (let ((node (mk-Node n nil prev)))
                                  (do
                                    (if prev (set! (Node-next prev) node) nil)
                                    (build (- n 1) node)))))))
            (build 1000 nil)))
    `
    expr, _ := parser.Parse(code)

    b.Run("compiled", func(b *testing.B) {
        for i := 0; i < b.N; i++ {
            jit.CompileAndRun(expr)
        }
    })
}

func BenchmarkChannels(b *testing.B) {
    code := `
        (let ((ch (make-chan 100)))
          (do
            (go (lambda ()
                  (letrec ((send (lambda (n)
                                   (if (= n 0)
                                       nil
                                       (do (chan-send! ch n)
                                           (send (- n 1)))))))
                    (send 1000))))
            (letrec ((recv (lambda (sum n)
                             (if (= n 0)
                                 sum
                                 (recv (+ sum (chan-recv! ch)) (- n 1))))))
              (recv 0 1000))))
    `
    expr, _ := parser.Parse(code)

    b.Run("compiled", func(b *testing.B) {
        for i := 0; i < b.N; i++ {
            jit.CompileAndRun(expr)
        }
    })
}
```

## Step 7.2: Memory Profiling

**File**: `test/benchmark/memory_test.go`
```go
package benchmark

import (
    "runtime"
    "testing"
)

func BenchmarkMemoryUsage(b *testing.B) {
    var m runtime.MemStats

    // Run test
    code := "(fold + 0 (range 100000))"
    expr, _ := parser.Parse(code)

    runtime.GC()
    runtime.ReadMemStats(&m)
    allocBefore := m.TotalAlloc

    for i := 0; i < b.N; i++ {
        jit.CompileAndRun(expr)
    }

    runtime.GC()
    runtime.ReadMemStats(&m)
    allocAfter := m.TotalAlloc

    b.ReportMetric(float64(allocAfter-allocBefore)/float64(b.N), "bytes/op")
}
```

---

# Phase 8: Stress Testing (Week 19-20)

## Step 8.1: Edge Case Tests

**File**: `test/stress/stress_test.go`
```go
package stress

import (
    "testing"
    "time"
)

func TestDeepRecursion(t *testing.T) {
    // Test stack overflow handling
    code := "(letrec ((f (lambda (n) (if (= n 0) 0 (+ 1 (f (- n 1))))))) (f 100000))"

    done := make(chan bool)
    go func() {
        defer func() {
            if r := recover(); r != nil {
                t.Logf("recovered from: %v", r)
            }
            done <- true
        }()

        expr, _ := parser.Parse(code)
        jit.CompileAndRun(expr)
    }()

    select {
    case <-done:
        // OK
    case <-time.After(10 * time.Second):
        t.Error("timeout")
    }
}

func TestLargeAllocation(t *testing.T) {
    // Test memory exhaustion handling
    code := "(letrec ((build (lambda (n) (if (= n 0) nil (cons n (build (- n 1))))))) (build 1000000))"

    expr, _ := parser.Parse(code)
    _, err := jit.CompileAndRun(expr)

    // Should either succeed or fail gracefully
    if err != nil {
        t.Logf("failed gracefully: %v", err)
    }
}

func TestManyGoroutines(t *testing.T) {
    code := `
        (let ((ch (make-chan 0)))
          (do
            (letrec ((spawn (lambda (n)
                              (if (= n 0)
                                  nil
                                  (do (go (lambda () (chan-send! ch n)))
                                      (spawn (- n 1)))))))
              (spawn 1000))
            (letrec ((collect (lambda (sum n)
                                (if (= n 0)
                                    sum
                                    (collect (+ sum (chan-recv! ch)) (- n 1))))))
              (collect 0 1000))))
    `

    expr, _ := parser.Parse(code)
    result, err := jit.CompileAndRun(expr)

    if err != nil {
        t.Fatalf("error: %v", err)
    }

    // Sum of 1 to 1000
    expected := 500500
    if result.Int != int64(expected) {
        t.Errorf("expected %d, got %d", expected, result.Int)
    }
}

func TestLongRunning(t *testing.T) {
    if testing.Short() {
        t.Skip("skipping long test")
    }

    // Run for extended period to check memory stability
    code := "(fold + 0 (range 1000))"
    expr, _ := parser.Parse(code)

    start := time.Now()
    iterations := 0

    for time.Since(start) < 60*time.Second {
        _, err := jit.CompileAndRun(expr)
        if err != nil {
            t.Fatalf("iteration %d failed: %v", iterations, err)
        }
        iterations++
    }

    t.Logf("completed %d iterations in 60 seconds", iterations)
}

func TestComplexCycles(t *testing.T) {
    // Create complex cyclic structure
    code := `
        (do
          (deftype Graph (nodes List))
          (deftype GNode (id int) (edges List) (back GNode :weak))

          (let ((n1 (mk-GNode 1 nil nil))
                (n2 (mk-GNode 2 nil nil))
                (n3 (mk-GNode 3 nil nil)))
            (do
              ; Create cycle: n1 -> n2 -> n3 -> n1
              (set! (GNode-edges n1) (list n2))
              (set! (GNode-edges n2) (list n3))
              (set! (GNode-edges n3) (list n1))

              ; Back edges
              (set! (GNode-back n2) n1)
              (set! (GNode-back n3) n2)
              (set! (GNode-back n1) n3)

              (GNode-id n1))))
    `

    expr, _ := parser.Parse(code)
    result, err := jit.CompileAndRun(expr)

    if err != nil {
        t.Fatalf("error: %v", err)
    }

    if result.Int != 1 {
        t.Errorf("expected 1, got %d", result.Int)
    }
}
```

---

# Summary: Complete Timeline

| Week | Phase | Focus |
|------|-------|-------|
| 1-2 | Phase 1 | Critical Validation (Valgrind, ASan, TSan, Correctness) |
| 3-4 | Phase 2 | RC Elimination |
| 5-6 | Phase 3 | Active Reuse Transformation |
| 7-8 | Phase 4 | Interprocedural Ownership |
| 9-12 | Phase 5 | DPS Code Generation |
| 13-16 | Phase 6 | Region Inference |
| 17-18 | Phase 7 | Performance Benchmarks |
| 19-20 | Phase 8 | Stress Testing |

---

# Checklist

## Phase 1: Critical Validation
- [ ] 1.1.1 Create Valgrind test harness
- [ ] 1.1.2 Create comprehensive test cases
- [ ] 1.1.3 Run initial validation
- [ ] 1.2.1 Create ASan test
- [ ] 1.3.1 Create TSan test
- [ ] 1.4.1 Create correctness comparison harness

## Phase 2: RC Elimination
- [ ] 2.1.1 Add uniqueness propagation
- [ ] 2.1.2 Add borrowed reference tracking
- [ ] 2.1.3 Generate optimized RC operations
- [ ] 2.1.4 Add RC elimination statistics
- [ ] 2.1.5 Test RC elimination

## Phase 3: Active Reuse
- [ ] 3.1.1 Track allocations with types
- [ ] 3.1.2 Modify GenerateLet to use reuse
- [ ] 3.1.3 Test reuse transformation

## Phase 4: Interprocedural Ownership
- [ ] 4.1.1 Query summaries during codegen
- [ ] 4.1.2 Auto-generate summaries for user functions
- [ ] 4.1.3 Test interprocedural optimization

## Phase 5: DPS Code Generation
- [ ] 5.1.1 Add DPS analysis
- [ ] 5.1.2 Generate DPS variants

## Phase 6: Region Inference
- [ ] 6.1.1 Create region analysis
- [ ] 6.1.2 Generate region-based allocation

## Phase 7: Performance Benchmarks
- [ ] 7.1 Create benchmark suite
- [ ] 7.2 Memory profiling

## Phase 8: Stress Testing
- [ ] 8.1 Edge case tests

## Phase 9: Memory Architecture Enhancements (Post‑11, ASAP‑Compatible)

These are optional extensions that **do not add language restrictions** and
**do not introduce stop‑the‑world GC**. They align with the ASAP‑first model.
For detailed sketches, see `docs/UNIFIED_OPTIMIZATION_PLAN.md`.

### 9.1 Linear/Offset Regions for Serialization & FFI

**Goal**: Allow regions to store pointers as offsets (or raw pointers) depending on
output target (file vs FFI) without per‑object fixups.

**Where to start (codebase)**:
- `runtime/src/memory/region.c` and `runtime/src/runtime.c` (region lifetime + pointer access)
- `runtime/src/memory/arena.c` (allocator patterns)
- `runtime/include/purple.h` (public runtime surface)

**Search terms**: `RegionContext`, `region_enter`, `region_alloc`, `region_ref_deref`,
`arena_create`, `arena_alloc`

**References**: Vale `LinearRegion` design (`Vale/docs/LinearRegion.md`)

**Tasks**:
- [ ] 9.1.1 Add `offset_mode` + `adjuster` to region state
- [ ] 9.1.2 Route pointer stores/loads through `region_store_ptr` / `region_deref_ptr`
- [ ] 9.1.3 Add tests for serialized buffers and file offsets

### 9.2 Pluggable Region Backends (IRegion‑style)

**Goal**: A small vtable lets the compiler choose arena/RC/pool/unsafe backends
without user annotations.

**Where to start (codebase)**:
- `runtime/src/memory/region.c` (region context)
- `runtime/src/memory/arena.c` (arena backend)
- `runtime/src/runtime.c` (allocation APIs exposed to generated code)

**Search terms**: `region_alloc`, `arena_alloc`, `dec_ref`, `free_tree`, `gen_*_runtime`

**References**: Vale `IRegion` interface (`Vale/docs/IRegion.md`)

**Tasks**:
- [ ] 9.2.1 Define a `RegionVTable` (alloc/free/deref/scan)
- [ ] 9.2.2 Implement arena + RC backends behind the vtable
- [ ] 9.2.3 Add codegen routing: escape/shape → backend selection

### 9.3 Weak Ref Control Blocks (Merge‑Friendly)

**Goal**: Make weak refs robust across region merges and arena teardown.

**Where to start (codebase)**:
- `runtime/src/runtime.c` (weak refs + object lifecycle)
- `runtime/include/purple.h` (weak ref public types)
- `docs/GENERATIONAL_MEMORY.md` (soundness constraints)

**Search terms**: `Weak`, `weak`, `invalidate_weak`, `BorrowRef`, `gen`

**References**: Vale weak ref options (`Vale/docs/WeakRef.md`)

**Tasks**:
- [ ] 9.3.1 Add `WeakCB` control block (gen + ptr + weak_count)
- [ ] 9.3.2 Make weak refs point to control blocks instead of objects
- [ ] 9.3.3 Invalidate by bumping `gen` + NULLing `ptr`

### 9.4 Transmigration / Isolation on Region Escape

**Goal**: When values escape a temporary region, isolate them so cross‑region borrows
become invalid without heap‑wide scans.

**Where to start (codebase)**:
- `csrc/analysis/analysis.c` (shape/escape information)
- `csrc/codegen/*` (where region boundaries are emitted)
- `runtime/src/runtime.c` (object headers + traversal utilities)

**Search terms**: `ShapeInfo`, `ESCAPE_`, `release_children`, `scan_`, `free_tree`

**References**: Vale transmigration (`Vale/docs/regions/Transmigration.md`)

**Tasks**:
- [ ] 9.4.1 Add `transmigrate_*` walkers (generation‑offset or copy‑out)
- [ ] 9.4.2 Hook into codegen when region‑local data escapes
- [ ] 9.4.3 Add tests: return from arena/pure region with borrows

### 9.5 External Handle Indexing (FFI + Determinism)

**Goal**: Stable external handles (index+generation) without exposing raw pointers,
optionally usable for deterministic record/replay.

**Where to start (codebase)**:
- `runtime/src/runtime.c` (GenRef/IPGE + handle utilities)
- `runtime/include/purple.h` (public API)
- `docs/GENERATIONAL_MEMORY.md` and `new_genref.md` (soundness + tagging)

**Search terms**: `BorrowRef`, `ipge_`, `generation`, `Handle`, `tag`

**References**: Vale replayability map (`Vale/docs/PerfectReplayability.md`)

**Tasks**:
- [ ] 9.5.1 Add `HandleTable` (index+gen → ptr)
- [ ] 9.5.2 Expose `handle_alloc/get/free` for FFI boundaries
- [ ] 9.5.3 Optional: map handles deterministically for replay
