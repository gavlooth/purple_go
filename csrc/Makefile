# OmniLisp Compiler - C Toolchain
# Replaces Go-based toolchain with pure C99 + POSIX implementation

CC = gcc
# Note: -pedantic removed to allow anonymous unions (widely supported extension)
CFLAGS = -std=c99 -Wall -Wextra -g -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
CFLAGS += -I. -I../third_party -I../runtime/include -I../omnilisp/src/runtime

LDFLAGS = -lpthread

# Sanitizer profiles
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer
TSAN_FLAGS = -fsanitize=thread
UBSAN_FLAGS = -fsanitize=undefined

# Source files
AST_SRCS = ast/ast.c
PARSER_SRCS = parser/parser.c parser/pika_core.c
ANALYSIS_SRCS = analysis/analysis.c
CODEGEN_SRCS = codegen/codegen.c
COMPILER_SRCS = compiler/compiler.c
CLI_SRCS = cli/main.c

# Object files
AST_OBJS = $(AST_SRCS:.c=.o)
PARSER_OBJS = $(PARSER_SRCS:.c=.o)
ANALYSIS_OBJS = $(ANALYSIS_SRCS:.c=.o)
CODEGEN_OBJS = $(CODEGEN_SRCS:.c=.o)
COMPILER_OBJS = $(COMPILER_SRCS:.c=.o)
CLI_OBJS = $(CLI_SRCS:.c=.o)

ALL_LIB_OBJS = $(AST_OBJS) $(PARSER_OBJS) $(ANALYSIS_OBJS) $(CODEGEN_OBJS) $(COMPILER_OBJS)

# Pika parser (from omnilisp - optional, we have embedded parser)
PIKA_DIR = ../omnilisp/src/runtime/pika_c
PIKA_SRC = $(PIKA_DIR)/pika.c
PIKA_OBJ = $(PIKA_DIR)/pika.o

# Static library
LIBRARY = libomnilisp.a

# Executable
TARGET = omnilisp

.PHONY: all clean debug release asan tsan ubsan test help

all: $(TARGET)

help:
	@echo "OmniLisp C Toolchain"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build the compiler (default)"
	@echo "  debug     - Build with debug symbols, no optimization"
	@echo "  release   - Build with full optimization"
	@echo "  asan      - Build with AddressSanitizer"
	@echo "  tsan      - Build with ThreadSanitizer"
	@echo "  ubsan     - Build with UndefinedBehaviorSanitizer"
	@echo "  test      - Run tests"
	@echo "  clean     - Remove build artifacts"
	@echo ""
	@echo "Usage:"
	@echo "  ./omnilisp -e '(+ 1 2)'         # Evaluate expression"
	@echo "  ./omnilisp -c file.omni         # Compile to C"
	@echo "  ./omnilisp -o prog file.omni    # Compile to binary"
	@echo "  ./omnilisp                      # Start REPL"

# Build modes
debug: CFLAGS += -DDEBUG -O0
debug: all

release: CFLAGS += -DNDEBUG -O3
release: all

asan: CFLAGS += $(ASAN_FLAGS)
asan: LDFLAGS += $(ASAN_FLAGS)
asan: all

tsan: CFLAGS += $(TSAN_FLAGS)
tsan: LDFLAGS += $(TSAN_FLAGS)
tsan: all

ubsan: CFLAGS += $(UBSAN_FLAGS)
ubsan: LDFLAGS += $(UBSAN_FLAGS)
ubsan: all

# Static library
$(LIBRARY): $(ALL_LIB_OBJS)
	ar rcs $@ $^

# Main executable
$(TARGET): $(CLI_OBJS) $(LIBRARY)
	$(CC) $(CFLAGS) -o $@ $(CLI_OBJS) -L. -lomnilisp $(LDFLAGS)

# Pattern rules
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Test target
test: $(TARGET)
	@echo "Running basic tests..."
	@echo "(+ 1 2)" | ./$(TARGET) && echo "PASS: arithmetic"
	@echo "(if (< 1 2) 10 20)" | ./$(TARGET) && echo "PASS: conditionals"
	@echo "(let [x 5] (* x x))" | ./$(TARGET) && echo "PASS: let bindings"
	@echo "(define (square n) (* n n)) (square 7)" | ./$(TARGET) && echo "PASS: functions"
	@echo "All basic tests passed!"

# Clean
clean:
	rm -f $(ALL_LIB_OBJS) $(CLI_OBJS) $(LIBRARY) $(TARGET)
	rm -f $(PIKA_OBJ)

# Dependencies
ast/ast.o: ast/ast.c ast/ast.h
parser/parser.o: parser/parser.c parser/parser.h ast/ast.h
analysis/analysis.o: analysis/analysis.c analysis/analysis.h ast/ast.h
codegen/codegen.o: codegen/codegen.c codegen/codegen.h ast/ast.h analysis/analysis.h
compiler/compiler.o: compiler/compiler.c compiler/compiler.h parser/parser.h analysis/analysis.h codegen/codegen.h
cli/main.o: cli/main.c compiler/compiler.h
