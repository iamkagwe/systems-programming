#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

typedef void (*task_fn)(void *arg);

typedef struct {
    task_fn func;
    void *arg;
} task_t;

typedef struct {
    pthread_t *threads;
    int num_threads;
    
    // Work queue
    task_t *queue;
    int queue_capacity;
    int queue_size;
    int queue_front;
    
    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t cond_work;    // signal: work available
    pthread_cond_t cond_empty;   // signal: queue is empty
    
    // State
    int shutdown;
} thread_pool_t;

/**
 * Create a thread pool with num_threads worker threads.
 * Returns NULL on failure.
 */
thread_pool_t *thread_pool_create(int num_threads);

/**
 * Submit a task to the pool.
 * Blocks if queue is full (capacity: num_threads * 10).
 * Returns 0 on success, -1 if pool is shutdown.
 */
int thread_pool_submit(thread_pool_t *pool, task_fn func, void *arg);

/**
 * Wait for all submitted tasks to complete.
 * Does not block new submissions.
 */
void thread_pool_wait_all(thread_pool_t *pool);

/**
 * Shutdown the pool. Blocks until all tasks complete.
 * New submissions after shutdown will fail.
 */
void thread_pool_destroy(thread_pool_t *pool);

#endif
