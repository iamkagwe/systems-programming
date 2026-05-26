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
    // Parse arguments: consumer ID (optional)
    int consumer_id = 0;
    if (argc > 1) {
        consumer_id = atoi(argv[1]);
    }
    
    printf("Consumer %d: Opening shared buffer\n", consumer_id);
    
    // Wait for producer to create buffer
    // Retry a few times in case producer is still starting
    SharedBuffer *buf = NULL;
    int retries = 10;
    while (!buf && retries > 0) {
        buf = buffer_open(0);  // 0 = consumer role
        if (!buf) {
            if (retries > 1) {
                usleep(100000);  // Sleep 100ms and retry
                retries--;
            } else {
                fprintf(stderr, "Consumer %d: Failed to open shared buffer\n", consumer_id);
                return 1;
            }
        }
    }
    
    printf("Consumer %d: Connected to shared buffer\n", consumer_id);
    
    // Setup signal handler for graceful exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Consumer's local read position (not shared)
    // Each consumer reads independently
    uint64_t my_read_pos = 0;
    
    // Measure read throughput
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Read messages
    char msg[MSG_SIZE];
    long messages_read = 0;
    
    // Read until we see 10 seconds of inactivity
    // (producer has likely finished)
    int idle_count = 0;
    while (running && idle_count < 100) {
        // Try to read without blocking (check if ahead of producer)
        if (my_read_pos >= buf->write_pos) {
            // No new messages, but don't exit immediately
            idle_count++;
            usleep(100000);  // Sleep 100ms
            continue;
        }
        
        idle_count = 0;  // Reset idle counter on successful read
        
        if (buffer_read(buf, msg, MSG_SIZE, &my_read_pos) > 0) {
            messages_read++;
            
            // Progress indicator
            if (messages_read % 10000 == 0) {
                printf("  Consumer %d: Read %ld messages\n", consumer_id, messages_read);
                fflush(stdout);
            }
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate elapsed time
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    
    if (elapsed == 0) elapsed = 0.001;  // Avoid divide by zero
    
    // Report statistics
    printf("\nConsumer %d: Finished reading\n", consumer_id);
    printf("  Messages read: %ld\n", messages_read);
    printf("  Time: %.3f seconds\n", elapsed);
    printf("  Throughput: %.1f msgs/sec\n", messages_read / elapsed);
    
    // Cleanup
    buffer_close(buf);
    
    return 0;
}
