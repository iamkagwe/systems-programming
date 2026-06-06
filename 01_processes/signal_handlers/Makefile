CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -pthread
LDFLAGS = -pthread

TARGETS = signal_server client

all: $(TARGETS)

signal_server: signal_server.c
	$(CC) $(CFLAGS) -o signal_server signal_server.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
