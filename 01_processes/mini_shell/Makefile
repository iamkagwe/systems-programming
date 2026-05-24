CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS = 

TARGET = shell
SRCS = shell.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

debug: CFLAGS += -DDEBUG
debug: clean $(TARGET)
	gdb ./$(TARGET)

valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.DEFAULT_GOAL := all
