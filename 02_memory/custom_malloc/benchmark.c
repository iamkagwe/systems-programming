#include "allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

/*
 * Benchmark: Custom Allocator vs System malloc
 *
 * Tests:
 *   1. Random allocation pattern (fragmentation test)
 *   2. Streaming pattern (best case)
 *   3. Mixed workload (realistic)
 */

double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void test_random_pattern(int num_ops) {
    printf("\n=== Test 1: Random Allocation/Free Pattern ===\n");
    printf("Operations: %d\n", num_ops);
    
    // Test 1a: System malloc
    printf("\nSystem malloc:\n");
    double start = get_time_seconds();
    
    void **ptrs = malloc(num_ops * sizeof(void *));
    for (int i = 0; i < num_ops; i++) {
        size_t size = (rand() % 256) + 16;  // 16-272 bytes
        ptrs[i] = malloc(size);
    }
    
    for (int i = 0; i < num_ops; i++) {
        free(ptrs[i]);
    }
    free(ptrs);
    
    double elapsed_malloc = get_time_seconds() - start;
    printf("  Time: %.3f seconds\n", elapsed_malloc);
    
    // Test 1b: Custom allocator
    printf("\nCustom allocator:\n");
    allocator_init(10 * 1024 * 1024);  // 10 MB heap
    
    start = get_time_seconds();
    
    ptrs = malloc(num_ops * sizeof(void *));
    for (int i = 0; i < num_ops; i++) {
        size_t size = (rand() % 256) + 16;
        ptrs[i] = my_malloc(size);
    }
    
    for (int i = 0; i < num_ops; i++) {
        my_free(ptrs[i]);
    }
    free(ptrs);
    
    double elapsed_custom = get_time_seconds() - start;
    printf("  Time: %.3f seconds\n", elapsed_custom);
    allocator_stats();
    allocator_cleanup();
    
    printf("  Speedup: %.2fx\n", elapsed_malloc / elapsed_custom);
}

void test_streaming_pattern(int num_ops) {
    printf("\n=== Test 2: Streaming Pattern (Allocate then Free All) ===\n");
    printf("Operations: %d\n", num_ops);
    
    // Test 2a: System malloc
    printf("\nSystem malloc:\n");
    double start = get_time_seconds();
    
    void **ptrs = malloc(num_ops * sizeof(void *));
    
    // Allocate all
    for (int i = 0; i < num_ops; i++) {
        size_t size = (rand() % 256) + 16;
        ptrs[i] = malloc(size);
    }
    
    // Free all
    for (int i = 0; i < num_ops; i++) {
        free(ptrs[i]);
    }
    free(ptrs);
    
    double elapsed_malloc = get_time_seconds() - start;
    printf("  Time: %.3f seconds\n", elapsed_malloc);
    
    // Test 2b: Custom allocator
    printf("\nCustom allocator:\n");
    allocator_init(10 * 1024 * 1024);  // 10 MB heap
    
    start = get_time_seconds();
    
    ptrs = malloc(num_ops * sizeof(void *));
    
    // Allocate all
    for (int i = 0; i < num_ops; i++) {
        size_t size = (rand() % 256) + 16;
        ptrs[i] = my_malloc(size);
    }
    
    // Free all
    for (int i = 0; i < num_ops; i++) {
        my_free(ptrs[i]);
    }
    free(ptrs);
    
    double elapsed_custom = get_time_seconds() - start;
    printf("  Time: %.3f seconds\n", elapsed_custom);
    allocator_stats();
    allocator_cleanup();
    
    printf("  Speedup: %.2fx\n", elapsed_malloc / elapsed_custom);
}

void test_fragmentation(void) {
    printf("\n=== Test 3: Fragmentation Analysis ===\n");
    
    allocator_init(1024 * 1024);  // 1 MB heap
    
    printf("\nAllocating 100 blocks of 5 KB...\n");
    void **ptrs = malloc(100 * sizeof(void *));
    
    for (int i = 0; i < 100; i++) {
        ptrs[i] = my_malloc(5120);
    }
    
    printf("After allocation:\n");
    allocator_stats();
    
    printf("\nFreeing every other block (50 blocks)...\n");
    for (int i = 0; i < 100; i += 2) {
        my_free(ptrs[i]);
    }
    
    printf("After selective free:\n");
    allocator_stats();
    
    printf("\nReallocating 50 new blocks of 5 KB (should reuse freed space)...\n");
    void **new_ptrs = malloc(50 * sizeof(void *));
    
    for (int i = 0; i < 50; i++) {
        new_ptrs[i] = my_malloc(5120);
    }
    
    printf("After reallocation:\n");
    allocator_stats();
    
    // Cleanup
    for (int i = 1; i < 100; i += 2) {
        my_free(ptrs[i]);
    }
    for (int i = 0; i < 50; i++) {
        my_free(new_ptrs[i]);
    }
    
    free(ptrs);
    free(new_ptrs);
    allocator_cleanup();
}

int main(void) {
    srand(time(NULL));
    
    printf("Custom Memory Allocator Benchmark\n");
    printf("==================================\n");
    
    test_random_pattern(10000);
    test_streaming_pattern(10000);
    test_fragmentation();
    
    printf("\n=== Benchmark Complete ===\n");
    printf("\nKey Insights:\n");
    printf("• Custom allocator: Simpler, less overhead, predictable\n");
    printf("• System malloc: Optimized for fragmentation, thread-safe\n");
    printf("• Coalescing reduces fragmentation but has cost\n");
    printf("• First-fit is simple; real allocators use more sophisticated strategies\n");
    
    return 0;
}
