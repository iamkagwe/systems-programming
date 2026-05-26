#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * SYNCHRONIZATION STRATEGY:
 * 
 * Mutex:
 *   Protects queue state: size, front, items[]
 *   Must hold mutex when reading/writing these fields
 * 
 * Condition Variables (with spurious wakeup handling):
 *   cond_full:  Signaled when an item is removed (queue has space)
 *               Producers wait here if queue is full
 *   cond_empty: Signaled when an item is added (queue has data)
 *               Consumers wait here if queue is empty
 * 
 * Pattern (producer):
 *   1. Lock mutex
 *   2. While queue is full: wait on cond_full (releases mutex, sleeps)
 *   3. Add item, advance front/size
 *   4. Signal cond_empty (wake any waiting consumers)
 *   5. Unlock mutex
 * 
 * Pattern (consumer):
 *   1. Lock mutex
 *   2. While queue is empty: wait on cond_empty (releases mutex, sleeps)
 *   3. Remove item, advance front/size
 *   4. Signal cond_full (wake any waiting producers)
 *   5. Unlock mutex
 */

Queue* queue_create(int capacity) {
    Queue *q = (Queue *)malloc(sizeof(Queue));
    if (!q) return NULL;
    
    q->items = (void **)malloc(sizeof(void *) * capacity);
    if (!q->items) {
        free(q);
        return NULL;
    }
    
    q->capacity = capacity;
    q->size = 0;
    q->front = 0;
    
    // Initialize mutex
    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        free(q->items);
        free(q);
        return NULL;
    }
    
    // Initialize condition variables
    if (pthread_cond_init(&q->cond_full, NULL) != 0) {
        pthread_mutex_destroy(&q->mutex);
        free(q->items);
        free(q);
        return NULL;
    }
    
    if (pthread_cond_init(&q->cond_empty, NULL) != 0) {
        pthread_cond_destroy(&q->cond_full);
        pthread_mutex_destroy(&q->mutex);
        free(q->items);
        free(q);
        return NULL;
    }
    
    return q;
}

void queue_destroy(Queue *q) {
    if (!q) return;
    
    pthread_cond_destroy(&q->cond_empty);
    pthread_cond_destroy(&q->cond_full);
    pthread_mutex_destroy(&q->mutex);
    
    if (q->items) free(q->items);
    free(q);
}

int queue_enqueue(Queue *q, void *item) {
    if (!q || !item) return -1;
    
    // Acquire mutex
    if (pthread_mutex_lock(&q->mutex) != 0) {
        return -1;
    }
    
    // Wait while queue is full
    // Note: use while loop to handle spurious wakeups
    // (condition variables can wake up without being signaled)
    while (q->size >= q->capacity) {
        // pthread_cond_wait releases mutex, sleeps, reacquires on wakeup
        if (pthread_cond_wait(&q->cond_full, &q->mutex) != 0) {
            pthread_mutex_unlock(&q->mutex);
            return -1;
        }
    }
    
    // Add item to back of queue
    // Circular buffer: (front + size) wraps around when reaching capacity
    int back = (q->front + q->size) % q->capacity;
    q->items[back] = item;
    q->size++;
    
    // Signal any waiting consumer that queue is no longer empty
    pthread_cond_signal(&q->cond_empty);
    
    // Release mutex
    pthread_mutex_unlock(&q->mutex);
    
    return 0;
}

void* queue_dequeue(Queue *q) {
    if (!q) return NULL;
    
    // Acquire mutex
    if (pthread_mutex_lock(&q->mutex) != 0) {
        return NULL;
    }
    
    // Wait while queue is empty
    // Use while loop for spurious wakeup handling
    while (q->size == 0) {
        // pthread_cond_wait releases mutex, sleeps, reacquires on wakeup
        if (pthread_cond_wait(&q->cond_empty, &q->mutex) != 0) {
            pthread_mutex_unlock(&q->mutex);
            return NULL;
        }
    }
    
    // Remove item from front
    void *item = q->items[q->front];
    q->front = (q->front + 1) % q->capacity;
    q->size--;
    
    // Signal any waiting producer that queue is no longer full
    pthread_cond_signal(&q->cond_full);
    
    // Release mutex
    pthread_mutex_unlock(&q->mutex);
    
    return item;
}

int queue_size(Queue *q) {
    if (!q) return -1;
    
    pthread_mutex_lock(&q->mutex);
    int size = q->size;
    pthread_mutex_unlock(&q->mutex);
    
    return size;
}

void* queue_try_dequeue(Queue *q) {
    if (!q) return NULL;
    
    // Try to acquire mutex without blocking
    if (pthread_mutex_trylock(&q->mutex) != 0) {
        return NULL;  // Mutex busy, return NULL
    }
    
    void *item = NULL;
    if (q->size > 0) {
        // Remove item from front
        item = q->items[q->front];
        q->front = (q->front + 1) % q->capacity;
        q->size--;
        
        // Signal any waiting producer that queue is no longer full
        pthread_cond_signal(&q->cond_full);
    }
    
    pthread_mutex_unlock(&q->mutex);
    
    return item;
}
