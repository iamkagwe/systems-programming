#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>

/*
 * Custom Memory Allocator
 *
 * Demonstrates heap management, fragmentation, and free list algorithms
 *
 * Concepts:
 *   - Memory layout: metadata + user data
 *   - Free list: linked list of available blocks
 *   - Fragmentation: external (between blocks) vs internal (within block)
 *   - Alignment: efficient access on modern CPUs
 */

/*
 * Initialize the allocator with a heap of given size
 * Must call before using my_malloc()
 */
void allocator_init(size_t heap_size);

/*
 * Allocate memory (similar to malloc)
 * Returns pointer to allocated block, or NULL on failure
 */
void* my_malloc(size_t size);

/*
 * Free allocated memory (similar to free)
 * Coalesces adjacent free blocks to reduce fragmentation
 */
void my_free(void *ptr);

/*
 * Get allocator statistics
 */
void allocator_stats(void);

/*
 * Cleanup allocator
 */
void allocator_cleanup(void);

#endif
