CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -pthread
LDFLAGS = -pthread

TARGETS = test

all: $(TARGETS)

test: test.c queue.c queue.h
	$(CC) $(CFLAGS) -o test test.c queue.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
