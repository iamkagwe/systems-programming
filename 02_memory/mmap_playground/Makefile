CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lm

TARGETS = benchmark vm_study

all: $(TARGETS)

benchmark: benchmark.c
	$(CC) $(CFLAGS) -o benchmark benchmark.c $(LDFLAGS)

vm_study: vm_study.c
	$(CC) $(CFLAGS) -o vm_study vm_study.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o /tmp/test_*.txt

.PHONY: all clean
