# Omnilisp Runtime Makefile

CC = gcc
CFLAGS = -Wall -Wextra -g -I./src/runtime

# Directories
RUNTIME_DIR = src/runtime
MEMORY_DIR = $(RUNTIME_DIR)/memory
UTIL_DIR = $(RUNTIME_DIR)/util
PIKA_DIR = $(RUNTIME_DIR)/pika
READER_DIR = $(RUNTIME_DIR)/reader

# Source files
SRCS = $(RUNTIME_DIR)/types.c \
       $(PIKA_DIR)/pika_core.c \
       $(READER_DIR)/omni_pika.c \
       $(UTIL_DIR)/dstring.c \
       $(UTIL_DIR)/hashmap.c \
       $(MEMORY_DIR)/scc.c \
       $(MEMORY_DIR)/deferred.c \
       $(MEMORY_DIR)/arena.c \
       $(MEMORY_DIR)/symmetric.c \
       $(MEMORY_DIR)/region.c \
       $(MEMORY_DIR)/genref.c \
       $(MEMORY_DIR)/constraint.c \
       $(MEMORY_DIR)/exception.c \
       $(MEMORY_DIR)/concurrent.c

# Object files
OBJS = $(SRCS:.c=.o)

# Executable
TARGET = omni

.PHONY: all clean

all: $(LIBRARY) $(TARGET)

$(LIBRARY): $(OBJS)
	ar rcs $@ $^

$(TARGET): $(LIBRARY) src/runtime/main.o
	$(CC) $(CFLAGS) -o $@ src/runtime/main.o -L. -lomniruntime

# Pattern rule for object files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

# Clean build artifacts
clean:
	rm -f $(OBJS) $(LIBRARY)
