# Purple Runtime Test Suite Makefile

CC ?= gcc
CFLAGS = -std=c99 -Wall -Wextra -g -I../include
LDFLAGS = -L.. -lpurple -lpthread -lm

# Source files
TESTS = test_main.c \
        test_constructors.c \
        test_memory.c \
        test_primitives.c

# Output
TEST_BIN = run_tests

# Sanitizer builds
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer
TSAN_FLAGS = -fsanitize=thread
UBSAN_FLAGS = -fsanitize=undefined

.PHONY: all clean test asan tsan ubsan

# Default: build and run tests
all: $(TEST_BIN)
	./$(TEST_BIN)

# Build test binary
$(TEST_BIN): $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) -o $@ $(TESTS) $(LDFLAGS)

# Build runtime if needed
../libpurple.a:
	$(MAKE) -C ..

# Run tests
test: $(TEST_BIN)
	./$(TEST_BIN)

# AddressSanitizer build
asan: $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $(TEST_BIN)_asan $(TESTS) $(LDFLAGS)
	./$(TEST_BIN)_asan

# ThreadSanitizer build
tsan: $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -o $(TEST_BIN)_tsan $(TESTS) $(LDFLAGS)
	./$(TEST_BIN)_tsan

# UndefinedBehaviorSanitizer build
ubsan: $(TESTS) ../libpurple.a
	$(CC) $(CFLAGS) $(UBSAN_FLAGS) -o $(TEST_BIN)_ubsan $(TESTS) $(LDFLAGS)
	./$(TEST_BIN)_ubsan

# Run with valgrind
valgrind: $(TEST_BIN)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TEST_BIN)

# Clean
clean:
	rm -f $(TEST_BIN) $(TEST_BIN)_asan $(TEST_BIN)_tsan $(TEST_BIN)_ubsan

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
