CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -pthread
LDFLAGS = -lpthread

# Targets
TARGETS = producer consumer

all: $(TARGETS)

producer: producer.c shared_buffer.c shared_buffer.h
	$(CC) $(CFLAGS) -o producer producer.c shared_buffer.c $(LDFLAGS)

consumer: consumer.c shared_buffer.c shared_buffer.h
	$(CC) $(CFLAGS) -o consumer consumer.c shared_buffer.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
