#include "shared_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

static volatile int running = 1;

void signal_handler(int sig __attribute__((unused))) {
    running = 0;
}

int main(int argc, char *argv[]) {
    // Parse arguments: number of messages to produce
    long num_messages = 100000;
    if (argc > 1) {
        num_messages = strtol(argv[1], NULL, 10);
    }
    
    printf("Producer: Creating shared buffer for %ld messages\n", num_messages);
    
    // Create shared buffer (producer role)
    SharedBuffer *buf = buffer_open(1);
    if (!buf) {
        fprintf(stderr, "Failed to create shared buffer\n");
        return 1;
    }
    
    // Setup signal handler for graceful exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Measure write throughput
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Produce messages
    char msg[MSG_SIZE];
    for (long i = 0; i < num_messages && running; i++) {
        // Create message: "msg_N" where N is the sequence number
        snprintf(msg, MSG_SIZE, "msg_%ld", i);
        
        if (buffer_write(buf, msg, strlen(msg)) < 0) {
            fprintf(stderr, "Failed to write message %ld\n", i);
            break;
        }
        
        // Progress indicator
        if (i % 10000 == 0 && i > 0) {
            printf("  Produced: %ld / %ld\n", i, num_messages);
            fflush(stdout);
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate elapsed time
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    
    // Wait for consumers to finish
    printf("\nProducer: Sent %ld messages\n", num_messages);
    printf("Time: %.3f seconds\n", elapsed);
    printf("Throughput: %.1f msgs/sec\n", num_messages / elapsed);
    printf("\nWaiting for consumers (press Ctrl+C to exit)...\n");
    
    // Keep process alive so consumers can read
    while (running) {
        sleep(1);
    }
    
    // Cleanup
    printf("\nProducer: Cleaning up shared memory\n");
    buffer_close(buf);
    buffer_unlink();
    
    return 0;
}
