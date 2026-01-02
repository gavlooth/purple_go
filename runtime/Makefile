# Purple Runtime Makefile
#
# Builds the Purple runtime as a static library (runtime.a)
# that can be linked with user-generated code.

CC ?= gcc
AR ?= ar
CFLAGS = -std=c99 -O2 -Wall -Wextra -fPIC
LDFLAGS = -lpthread

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Output
STATIC_LIB = libpurple.a
SHARED_LIB = libpurple.so

# Default target
all: $(BUILDDIR) $(STATIC_LIB)

# Create build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Compile source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/purple.h
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

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
