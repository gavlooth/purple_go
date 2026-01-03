CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -I..

SRCS = ../pika.c test_pika.c
OBJS = $(SRCS:.c=.o)
TARGET = test_pika

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
