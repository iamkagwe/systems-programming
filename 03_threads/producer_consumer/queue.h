#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

/*
 * Thread-safe bounded queue
 * 
 * Single producer, multiple consumers (or vice versa)
 * Uses mutex for mutual exclusion and condition variables for signaling
 */

typedef struct {
    void **items;           // Array of item pointers
    int capacity;           // Max items in queue
    int size;               // Current items in queue
    int front;              // Index of first item (for circular buffer)
    
    pthread_mutex_t mutex;  // Protects all access to queue
    pthread_cond_t cond_full;   // Signaled when queue is full (producers wait)
    pthread_cond_t cond_empty;  // Signaled when queue is empty (consumers wait)
} Queue;

/*
 * Create a bounded queue with given capacity
 * Returns: pointer to Queue, or NULL on error
 */
Queue* queue_create(int capacity);

/*
 * Destroy queue and free resources
 */
void queue_destroy(Queue *q);

/*
 * Enqueue item (blocking if queue is full)
 * 
 * Thread-safe: can be called from multiple producer threads
 * Returns: 0 on success, -1 on error
 */
int queue_enqueue(Queue *q, void *item);

/*
 * Dequeue item (blocking if queue is empty)
 * 
 * Thread-safe: can be called from multiple consumer threads
 * Returns: pointer to dequeued item, NULL on error
 */
void* queue_dequeue(Queue *q);

/*
 * Try to dequeue item (non-blocking)
 * Returns: pointer to item if available, NULL if queue is empty (not an error)
 */
void* queue_try_dequeue(Queue *q);

/*
 * Get current size (advisory only, racy check)
 */
int queue_size(Queue *q);

#endif
