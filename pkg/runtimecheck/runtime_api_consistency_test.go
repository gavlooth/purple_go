package runtimecheck

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

func repoRoot(t *testing.T) string {
	dir, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	for {
		if _, err := os.Stat(filepath.Join(dir, "go.mod")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			t.Fatalf("could not find repo root (go.mod) from %s", dir)
		}
		dir = parent
	}
}

func readFile(t *testing.T, path string) string {
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return string(b)
}

func minPositive(indices ...int) int {
	min := -1
	for _, idx := range indices {
		if idx < 0 {
			continue
		}
		if min == -1 || idx < min {
			min = idx
		}
	}
	return min
}

func extractStructBlock(t *testing.T, content, name, path string) string {
	start := strings.Index(content, "typedef struct "+name)
	if start == -1 {
		t.Fatalf("struct %s not found in %s", name, path)
	}
	open := strings.Index(content[start:], "{")
	if open == -1 {
		t.Fatalf("struct %s missing '{' in %s", name, path)
	}
	open += start
	close := strings.Index(content[open:], "};")
	if close == -1 {
		t.Fatalf("struct %s missing '};' in %s", name, path)
	}
	close += open + 2
	return content[open:close]
}

func hasFunc(content, name string) bool {
	re := regexp.MustCompile(`(?m)\b` + regexp.QuoteMeta(name) + `\s*\(`)
	return re.FindStringIndex(content) != nil
}

func TestGenerationTypedefBeforeBorrowRef(t *testing.T) {
	root := repoRoot(t)
	headerPath := filepath.Join(root, "runtime", "include", "purple.h")
	header := readFile(t, headerPath)

	borrowIdx := strings.Index(header, "typedef struct BorrowRef")
	if borrowIdx == -1 {
		t.Fatalf("BorrowRef typedef not found in %s", headerPath)
	}

	genIdx := minPositive(
		strings.Index(header, "typedef uint16_t Generation"),
		strings.Index(header, "typedef uint64_t Generation"),
	)
	if genIdx == -1 {
		t.Fatalf("Generation typedef not found in %s", headerPath)
	}

	if genIdx > borrowIdx {
		t.Fatalf("Generation typedef must appear before BorrowRef in %s (gen at %d, BorrowRef at %d)", headerPath, genIdx, borrowIdx)
	}
}

func TestObjLayoutTetheredConsistency(t *testing.T) {
	root := repoRoot(t)
	headerPath := filepath.Join(root, "runtime", "include", "purple.h")
	runtimePath := filepath.Join(root, "runtime", "src", "runtime.c")

	headerBlock := extractStructBlock(t, readFile(t, headerPath), "Obj", headerPath)
	runtimeBlock := extractStructBlock(t, readFile(t, runtimePath), "Obj", runtimePath)

	headerHas := strings.Contains(headerBlock, "tethered")
	runtimeHas := strings.Contains(runtimeBlock, "tethered")

	if headerHas != runtimeHas {
		t.Fatalf("Obj layout mismatch: header tethered=%v, runtime tethered=%v", headerHas, runtimeHas)
	}
}

func TestPublicApiSymbolsPresent(t *testing.T) {
	root := repoRoot(t)
	runtimePath := filepath.Join(root, "runtime", "src", "runtime.c")
	runtime := readFile(t, runtimePath)

	expected := []string{
		"borrow_get",
		"make_channel",
		"make_atom",
		"atom_cas",
		"spawn_thread",
		"channel_send",
	}

	for _, name := range expected {
		if !hasFunc(runtime, name) {
			t.Fatalf("runtime missing public API symbol %q (declared in header)", name)
		}
	}
}

func TestChannelSendSignatureMatchesHeader(t *testing.T) {
	root := repoRoot(t)
	headerPath := filepath.Join(root, "runtime", "include", "purple.h")
	runtimePath := filepath.Join(root, "runtime", "src", "runtime.c")
	header := readFile(t, headerPath)
	runtime := readFile(t, runtimePath)

	headerInt := regexp.MustCompile(`(?m)^\s*int\s+channel_send\s*\(`).FindStringIndex(header) != nil
	runtimeBool := regexp.MustCompile(`(?m)^\s*(?:static\s+)?bool\s+channel_send\s*\(`).FindStringIndex(runtime) != nil
	runtimeInt := regexp.MustCompile(`(?m)^\s*(?:static\s+)?int\s+channel_send\s*\(`).FindStringIndex(runtime) != nil

	if headerInt && !(runtimeBool || runtimeInt) {
		t.Fatalf("channel_send signature mismatch: header declares int, runtime does not define compatible return type")
	}
}

func TestChannelCapacityAllowsZero(t *testing.T) {
	root := repoRoot(t)
	runtimePath := filepath.Join(root, "runtime", "src", "runtime.c")
	runtime := readFile(t, runtimePath)

	if strings.Contains(runtime, "capacity > 0 ? capacity : 1") {
		t.Fatalf("channel capacity 0 is forced to 1; unbuffered channels should preserve 0 capacity")
	}
}
