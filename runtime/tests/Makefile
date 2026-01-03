# Purple Runtime Test Suite Makefile

CC ?= gcc
CFLAGS = -std=c99 -Wall -Wextra -g -I../include
LDFLAGS = -L.. -lpurple -lpthread -lm

# Source files - only test_main.c (it #includes all other test files and runtime.c)
TESTS = test_main.c

# Output
TEST_BIN = run_tests
API_TEST_BIN = run_tests_api

# Sanitizer builds
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer
TSAN_FLAGS = -fsanitize=thread
UBSAN_FLAGS = -fsanitize=undefined

.PHONY: all clean test fast slow api asan tsan ubsan asan-slow tsan-slow ubsan-slow

# Default: build and run tests
all: $(TEST_BIN)
	./$(TEST_BIN)

# Build test binary
$(TEST_BIN): $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) -o $@ $(TESTS) $(LDFLAGS)

# Build public-API test binary (header + library only)
$(API_TEST_BIN): test_api.c ../libpurple.a
	$(CC) $(CFLAGS) -o $@ test_api.c $(LDFLAGS)

# Build runtime if needed
../libpurple.a:
	$(MAKE) -C ..

# Run tests
test: $(TEST_BIN)
	./$(TEST_BIN)

# Fast/slow convenience targets
fast: $(TEST_BIN)
	./$(TEST_BIN)

slow: $(TEST_BIN)
	RUNTIME_TEST_LEVEL=slow ./$(TEST_BIN)

api: $(API_TEST_BIN)
	./$(API_TEST_BIN)

# AddressSanitizer build
asan: $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $(TEST_BIN)_asan $(TESTS) $(LDFLAGS)
	./$(TEST_BIN)_asan

asan-slow: $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $(TEST_BIN)_asan $(TESTS) $(LDFLAGS)
	RUNTIME_TEST_LEVEL=slow ./$(TEST_BIN)_asan

# ThreadSanitizer build
tsan: $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -o $(TEST_BIN)_tsan $(TESTS) $(LDFLAGS)
	./$(TEST_BIN)_tsan

tsan-slow: $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -o $(TEST_BIN)_tsan $(TESTS) $(LDFLAGS)
	RUNTIME_TEST_LEVEL=slow ./$(TEST_BIN)_tsan

# UndefinedBehaviorSanitizer build
ubsan: $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) $(UBSAN_FLAGS) -o $(TEST_BIN)_ubsan $(TESTS) $(LDFLAGS)
	./$(TEST_BIN)_ubsan

ubsan-slow: $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) $(UBSAN_FLAGS) -o $(TEST_BIN)_ubsan $(TESTS) $(LDFLAGS)
	RUNTIME_TEST_LEVEL=slow ./$(TEST_BIN)_ubsan

# Run with valgrind
valgrind: $(TEST_BIN)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TEST_BIN)

# Clean
clean:
	rm -f $(TEST_BIN) $(API_TEST_BIN) $(TEST_BIN)_asan $(TEST_BIN)_tsan $(TEST_BIN)_ubsan

# Help
help:
	@echo "Targets:"
	@echo "  all      - Build and run tests (default)"
	@echo "  test     - Run tests"
	@echo "  asan     - Run with AddressSanitizer"
	@echo "  tsan     - Run with ThreadSanitizer"
	@echo "  ubsan    - Run with UndefinedBehaviorSanitizer"
	@echo "  valgrind - Run with Valgrind"
	@echo "  clean    - Remove binaries"
