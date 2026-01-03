# Purple Runtime Makefile
#
# Builds the Purple runtime as a static library (libpurple.a)
# that can be linked with user-generated code.
#
# Now includes modular memory management from OmniLisp runtime.

CC ?= gcc
AR ?= ar
CFLAGS = -std=c99 -O2 -Wall -Wextra -fPIC -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -lpthread

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build

# Source files - main runtime
MAIN_SOURCES = $(wildcard $(SRCDIR)/*.c)
MAIN_OBJECTS = $(MAIN_SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Memory module sources - These are compiler analysis modules, not runtime code
# They are kept for reference but not compiled into libpurple.a
# MEMORY_SOURCES = $(wildcard $(SRCDIR)/memory/*.c)
# MEMORY_OBJECTS = $(MEMORY_SOURCES:$(SRCDIR)/memory/%.c=$(BUILDDIR)/memory/%.o)

# Utility module sources - useful helpers for the runtime
UTIL_SOURCES = $(wildcard $(SRCDIR)/util/*.c)
UTIL_OBJECTS = $(UTIL_SOURCES:$(SRCDIR)/util/%.c=$(BUILDDIR)/util/%.o)

# All source files (excluding memory and util modules for now)
SOURCES = $(MAIN_SOURCES)
OBJECTS = $(MAIN_OBJECTS)

# Output
STATIC_LIB = libpurple.a
SHARED_LIB = libpurple.so

# Default target
all: $(BUILDDIR) $(STATIC_LIB)

# Create build directories
$(BUILDDIR):
	mkdir -p $(BUILDDIR) $(BUILDDIR)/util

# Compile main source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/purple.h
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -c $< -o $@

# Compile utility module files
$(BUILDDIR)/util/%.o: $(SRCDIR)/util/%.c
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -c $< -o $@

# Build static library
$(STATIC_LIB): $(OBJECTS)
	$(AR) rcs $@ $^
	@echo "Built static library: $@"

# Build shared library (optional)
shared: $(BUILDDIR) $(OBJECTS)
	$(CC) -shared $(OBJECTS) -o $(SHARED_LIB) $(LDFLAGS)
	@echo "Built shared library: $(SHARED_LIB)"

# Install to system (optional)
install: $(STATIC_LIB)
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/purple
	install -m 644 $(STATIC_LIB) $(DESTDIR)/usr/local/lib/
	install -m 644 $(INCDIR)/purple.h $(DESTDIR)/usr/local/include/purple/

# Clean build artifacts
clean:
	rm -rf $(BUILDDIR) $(STATIC_LIB) $(SHARED_LIB)

# Test compilation
test: $(STATIC_LIB)
	@echo "Testing runtime compilation..."
	@echo 'int main() { return 0; }' > /tmp/test_runtime.c
	$(CC) -I$(INCDIR) /tmp/test_runtime.c -L. -lpurple $(LDFLAGS) -o /tmp/test_runtime
	@rm -f /tmp/test_runtime /tmp/test_runtime.c
	@echo "Runtime compiles successfully!"

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: clean all

# Optimized build
release: CFLAGS += -O3 -DNDEBUG
release: clean all

.PHONY: all clean install test debug release shared
