#include "thread_pool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define QUEUE_CAPACITY_MULTIPLIER 10

static void *worker_thread(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->mutex);
        
        // Wait until there's work or we're shutting down
        while (pool->queue_size == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->cond_work, &pool->mutex);
        }
        
        // If shutdown and queue is empty, exit
        if (pool->queue_size == 0 && pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        
        // Dequeue a task
        task_t task = pool->queue[pool->queue_front];
        pool->queue_front = (pool->queue_front + 1) % pool->queue_capacity;
        pool->queue_size--;
        
        // Signal if queue was full and is now not full
        if (pool->queue_size == pool->queue_capacity - 1) {
            pthread_cond_broadcast(&pool->cond_work);
        }
        
        // Signal if queue is now empty
        if (pool->queue_size == 0) {
            pthread_cond_signal(&pool->cond_empty);
        }
        
        pthread_mutex_unlock(&pool->mutex);
        
        // Execute task (outside lock)
        if (task.func) {
            task.func(task.arg);
        }
    }
    
    return NULL;
}

thread_pool_t *thread_pool_create(int num_threads) {
    if (num_threads <= 0) {
        fprintf(stderr, "Invalid number of threads: %d\n", num_threads);
        return NULL;
    }
    
    thread_pool_t *pool = malloc(sizeof(thread_pool_t));
    if (!pool) {
        perror("malloc");
        return NULL;
    }
    
    pool->num_threads = num_threads;
    pool->queue_capacity = num_threads * QUEUE_CAPACITY_MULTIPLIER;
    pool->queue_size = 0;
    pool->queue_front = 0;
    pool->shutdown = 0;
    
    // Allocate queue
    pool->queue = malloc(pool->queue_capacity * sizeof(task_t));
    if (!pool->queue) {
        perror("malloc");
        free(pool);
        return NULL;
    }
    
    // Allocate thread array
    pool->threads = malloc(num_threads * sizeof(pthread_t));
    if (!pool->threads) {
        perror("malloc");
        free(pool->queue);
        free(pool);
        return NULL;
    }
    
    // Initialize synchronization
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        free(pool->threads);
        free(pool->queue);
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->cond_work, NULL) != 0) {
        perror("pthread_cond_init");
        pthread_mutex_destroy(&pool->mutex);
        free(pool->threads);
        free(pool->queue);
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->cond_empty, NULL) != 0) {
        perror("pthread_cond_init");
        pthread_cond_destroy(&pool->cond_work);
        pthread_mutex_destroy(&pool->mutex);
        free(pool->threads);
        free(pool->queue);
        free(pool);
        return NULL;
    }
    
    // Create worker threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            perror("pthread_create");
            // Cleanup: shutdown and wait for threads that were created
            pool->shutdown = 1;
            pthread_cond_broadcast(&pool->cond_work);
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            pthread_cond_destroy(&pool->cond_empty);
            pthread_cond_destroy(&pool->cond_work);
            pthread_mutex_destroy(&pool->mutex);
            free(pool->threads);
            free(pool->queue);
            free(pool);
            return NULL;
        }
    }
    
    return pool;
}

int thread_pool_submit(thread_pool_t *pool, task_fn func, void *arg) {
    if (!pool || !func) {
        return -1;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }
    
    // Wait if queue is full
    while (pool->queue_size >= pool->queue_capacity) {
        pthread_cond_wait(&pool->cond_work, &pool->mutex);
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            return -1;
        }
    }
    
    // Enqueue task
    int queue_rear = (pool->queue_front + pool->queue_size) % pool->queue_capacity;
    pool->queue[queue_rear].func = func;
    pool->queue[queue_rear].arg = arg;
    pool->queue_size++;
    
    // Wake up one worker
    pthread_cond_signal(&pool->cond_work);
    
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

void thread_pool_wait_all(thread_pool_t *pool) {
    if (!pool) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    // Wait until queue is empty
    while (pool->queue_size > 0 && !pool->shutdown) {
        pthread_cond_wait(&pool->cond_empty, &pool->mutex);
    }
    
    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_destroy(thread_pool_t *pool) {
    if (!pool) {
        return;
    }
    
    // Wait for all tasks to complete first
    thread_pool_wait_all(pool);
    
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->cond_work);
    pthread_mutex_unlock(&pool->mutex);
    
    // Wait for all threads to finish
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // Cleanup
    pthread_cond_destroy(&pool->cond_empty);
    pthread_cond_destroy(&pool->cond_work);
    pthread_mutex_destroy(&pool->mutex);
    free(pool->threads);
    free(pool->queue);
    free(pool);
}
