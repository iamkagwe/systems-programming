CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -std=c99
LDFLAGS = -pthread

TARGETS = test

all: $(TARGETS)

test: test.c thread_pool.c thread_pool.h
	$(CC) $(CFLAGS) -o test test.c thread_pool.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
