CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -std=c99
LDFLAGS = -pthread

TARGETS = test

all: $(TARGETS)

test: test.c lru_cache.c lru_cache.h
	$(CC) $(CFLAGS) -o test test.c lru_cache.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
