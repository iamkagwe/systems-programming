#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

static volatile int tasks_completed = 0;
static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;

void simple_task(void *arg) {
    (void)arg;  // unused
    // Simulate work: small computation
    long sum = 0;
    for (int i = 0; i < 100000; i++) {
        sum += i;
    }
    (void)sum;  // avoid unused variable warning
    
    pthread_mutex_lock(&counter_lock);
    tasks_completed++;
    pthread_mutex_unlock(&counter_lock);
}

void cpu_bound_task(void *arg) {
    (void)arg;  // unused
    // Simulate more intensive work
    long sum = 0;
    for (int i = 0; i < 1000000; i++) {
        sum += i * i;
    }
    (void)sum;  // avoid unused variable warning
    
    pthread_mutex_lock(&counter_lock);
    tasks_completed++;
    pthread_mutex_unlock(&counter_lock);
}

double get_time_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void benchmark(int num_threads, int num_tasks) {
    printf("\n=== Benchmark: %d threads, %d tasks ===\n", num_threads, num_tasks);
    
    thread_pool_t *pool = thread_pool_create(num_threads);
    if (!pool) {
        printf("Failed to create thread pool\n");
        return;
    }
    
    tasks_completed = 0;
    double start = get_time_seconds();
    
    // Submit all tasks
    for (int i = 0; i < num_tasks; i++) {
        if (thread_pool_submit(pool, simple_task, (void *)(intptr_t)i) != 0) {
            printf("Failed to submit task %d\n", i);
            break;
        }
    }
    
    // Wait for completion
    thread_pool_wait_all(pool);
    double elapsed = get_time_seconds() - start;
    
    printf("Tasks completed: %d\n", tasks_completed);
    printf("Time: %.3f seconds\n", elapsed);
    printf("Throughput: %.0f tasks/sec\n", num_tasks / elapsed);
    printf("Time per task: %.3f microseconds\n", (elapsed / num_tasks) * 1e6);
    
    thread_pool_destroy(pool);
}

int main(void) {
    printf("Thread Pool Benchmark\n");
    printf("=====================\n");
    
    // Test 1: Correctness with small task
    printf("\n--- Correctness Test ---\n");
    thread_pool_t *pool = thread_pool_create(4);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        return 1;
    }
    
    tasks_completed = 0;
    int test_tasks = 100;
    for (int i = 0; i < test_tasks; i++) {
        thread_pool_submit(pool, simple_task, (void *)(intptr_t)i);
    }
    
    thread_pool_wait_all(pool);
    printf("Submitted %d tasks, completed %d tasks\n", test_tasks, tasks_completed);
    if (tasks_completed == test_tasks) {
        printf("✓ Correctness test PASSED\n");
    } else {
        printf("✗ Correctness test FAILED\n");
    }
    
    thread_pool_destroy(pool);
    
    // Test 2: Scaling benchmarks
    printf("\n--- Scaling Benchmarks ---\n");
    printf("(All with 10,000 tasks)\n");
    
    for (int threads = 1; threads <= 16; threads *= 2) {
        benchmark(threads, 10000);
    }
    
    // Test 3: High load
    printf("\n--- High Load Benchmark ---\n");
    benchmark(8, 100000);
    
    return 0;
}
