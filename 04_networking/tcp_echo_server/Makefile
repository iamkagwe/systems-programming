CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -std=c99
LDFLAGS = -pthread

TARGETS = server_threaded server_epoll client

all: $(TARGETS)

server_threaded: server_threaded.c
	$(CC) $(CFLAGS) -o server_threaded server_threaded.c $(LDFLAGS)

server_epoll: server_epoll.c
	$(CC) $(CFLAGS) -o server_epoll server_epoll.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
