#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

/*
 * Producer-Consumer Benchmark
 * 
 * Tests thread-safe queue with:
 * - Configurable number of producer/consumer threads
 * - Configurable number of items to produce
 * - Measures throughput and contention effects
 */

typedef struct {
    long id;
    long items_produced;
    long items_consumed;
} ThreadStats;

Queue *q = NULL;
long num_items = 100000;
int num_producers = 1;
int num_consumers = 2;

void* producer_thread(void *arg) {
    ThreadStats *stats = (ThreadStats *)arg;
    stats->items_produced = 0;
    
    for (long i = 0; i < num_items; i++) {
        // Allocate item (encode with producer ID for verification)
        long *item = (long *)malloc(sizeof(long));
        *item = (stats->id << 32) | (i & 0xFFFFFFFF);
        
        if (queue_enqueue(q, item) == 0) {
            stats->items_produced++;
        } else {
            free(item);
        }
        
        // Progress indicator
        if (stats->items_produced % 10000 == 0) {
            printf("Producer %ld: produced %ld items\n", 
                   stats->id, stats->items_produced);
            fflush(stdout);
        }
    }
    
    printf("Producer %ld: Finished (total: %ld)\n", 
           stats->id, stats->items_produced);
    return stats;
}

void* consumer_thread(void *arg) {
    ThreadStats *stats = (ThreadStats *)arg;
    stats->items_consumed = 0;
    
    // Exit after N iterations of not finding items
    // This lets us know producers are done
    int empty_iterations = 0;
    int max_empty_iterations = 1000;  // ~100ms timeout at 100us sleep
    
    while (empty_iterations < max_empty_iterations) {
        // Try non-blocking read
        long *item = (long *)queue_try_dequeue(q);
        
        if (item) {
            empty_iterations = 0;  // Reset empty counter
            stats->items_consumed++;
            free(item);
            
            // Progress indicator
            if (stats->items_consumed % 10000 == 0) {
                printf("Consumer %ld: consumed %ld items\n", 
                       stats->id, stats->items_consumed);
                fflush(stdout);
            }
        } else {
            // Queue empty, increment empty counter
            empty_iterations++;
            usleep(100);  // Sleep 100us to avoid busy-waiting
        }
    }
    
    printf("Consumer %ld: Finished (total: %ld)\n", 
           stats->id, stats->items_consumed);
    return stats;
}

int main(int argc, char *argv[]) {
    // Parse arguments
    if (argc > 1) num_items = strtol(argv[1], NULL, 10);
    if (argc > 2) num_producers = atoi(argv[2]);
    if (argc > 3) num_consumers = atoi(argv[3]);
    
    printf("Producer-Consumer Queue Benchmark\n");
    printf("==================================\n");
    printf("Items: %ld\n", num_items);
    printf("Producers: %d\n", num_producers);
    printf("Consumers: %d\n", num_consumers);
    printf("Queue capacity: 100\n\n");
    
    // Create queue
    q = queue_create(100);
    if (!q) {
        fprintf(stderr, "Failed to create queue\n");
        return 1;
    }
    
    int total_threads = num_producers + num_consumers;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * total_threads);
    ThreadStats *stats = (ThreadStats *)malloc(sizeof(ThreadStats) * total_threads);
    
    if (!threads || !stats) {
        fprintf(stderr, "Failed to allocate thread structures\n");
        return 1;
    }
    
    // Start measurement
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Create producer threads
    printf("Creating %d producer threads...\n", num_producers);
    for (int i = 0; i < num_producers; i++) {
        stats[i].id = i;
        stats[i].items_produced = 0;
        if (pthread_create(&threads[i], NULL, producer_thread, &stats[i]) != 0) {
            fprintf(stderr, "Failed to create producer thread %d\n", i);
            return 1;
        }
    }
    
    // Create consumer threads
    printf("Creating %d consumer threads...\n\n", num_consumers);
    for (int i = 0; i < num_consumers; i++) {
        int thread_id = num_producers + i;
        stats[thread_id].id = i;
        stats[thread_id].items_consumed = 0;
        if (pthread_create(&threads[thread_id], NULL, consumer_thread, 
                          &stats[thread_id]) != 0) {
            fprintf(stderr, "Failed to create consumer thread %d\n", i);
            return 1;
        }
    }
    
    // Wait for all threads to finish
    long total_produced = 0;
    long total_consumed = 0;
    
    for (int i = 0; i < num_producers; i++) {
        pthread_join(threads[i], NULL);
        total_produced += stats[i].items_produced;
    }
    
    for (int i = 0; i < num_consumers; i++) {
        int thread_id = num_producers + i;
        pthread_join(threads[thread_id], NULL);
        total_consumed += stats[thread_id].items_consumed;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate elapsed time
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    
    // Report results
    printf("\n=== Results ===\n");
    printf("Total produced: %ld\n", total_produced);
    printf("Total consumed: %ld\n", total_consumed);
    printf("Time: %.3f seconds\n", elapsed);
    printf("Throughput: %.1f items/sec\n", (total_produced + total_consumed) / (2 * elapsed));
    printf("Queue final size: %d\n", queue_size(q));
    
    // Cleanup
    queue_destroy(q);
    free(threads);
    free(stats);
    
    return 0;
}
