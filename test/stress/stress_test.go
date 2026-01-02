package stress

import (
	"testing"
	"time"

	"purple_go/pkg/eval"
	"purple_go/pkg/jit"
	"purple_go/pkg/parser"
)

func requireJIT(t *testing.T) {
	j := jit.Get()
	if !j.IsAvailable() {
		t.Skip("JIT not available (gcc not found)")
	}
}

func TestDeepRecursion(t *testing.T) {
	requireJIT(t)

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
		_, _ = jit.CompileAndRun(expr)
	}()

	select {
	case <-done:
		// OK
	case <-time.After(10 * time.Second):
		t.Error("timeout")
	}
}

func TestLargeAllocation(t *testing.T) {
	requireJIT(t)

	code := "(letrec ((build (lambda (n) (if (= n 0) nil (cons n (build (- n 1))))))) (build 1000000))"

	expr, _ := parser.Parse(code)
	_, err := jit.CompileAndRun(expr)

	if err != nil {
		t.Logf("failed gracefully: %v", err)
	}
}

func TestManyThreads(t *testing.T) {
	requireJIT(t)

	// Use thread (OS threads) with OS channels for true concurrency
	code := `
        (let ((ch (make-chan 0)))
          (do
            (letrec ((spawn (lambda (n)
                              (if (= n 0)
                                  nil
                                  (do (thread (chan-send! ch n))
                                      (spawn (- n 1)))))))
              (spawn 1000))
            (letrec ((collect (lambda (sum n)
                                (if (= n 0)
                                    sum
                                    (collect (+ sum (chan-recv! ch)) (- n 1))))))
              (collect 0 1000))))
    `

	expr, _ := parser.Parse(code)
	interp := eval.Eval(expr, eval.NewEnv())
	result, err := jit.CompileAndRun(interp)
	if err != nil {
		t.Fatalf("error: %v", err)
	}

	expected := int64(500500)
	if result.Int != expected {
		t.Errorf("expected %d, got %d", expected, result.Int)
	}
}

func TestLongRunning(t *testing.T) {
	requireJIT(t)

	if testing.Short() {
		t.Skip("skipping long test")
	}

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
	requireJIT(t)

	code := `
        (do
          (deftype Graph (nodes List))
          (deftype GNode (id int) (edges List) (back GNode :weak))

          (let ((n1 (mk-GNode 1 nil nil))
                (n2 (mk-GNode 2 nil nil))
                (n3 (mk-GNode 3 nil nil)))
            (do
              (set! (GNode-edges n1) (list n2))
              (set! (GNode-edges n2) (list n3))
              (set! (GNode-edges n3) (list n1))

              (set! (GNode-back n2) n1)
              (set! (GNode-back n3) n2)
              (set! (GNode-back n1) n3)

              (GNode-id n1))))
    `

	expr, _ := parser.Parse(code)
	interp := eval.Eval(expr, eval.NewEnv())
	result, err := jit.CompileAndRun(interp)
	if err != nil {
		t.Fatalf("error: %v", err)
	}

	if result.Int != 1 {
		t.Errorf("expected 1, got %d", result.Int)
	}
}
