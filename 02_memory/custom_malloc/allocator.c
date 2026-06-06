#include "allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * MEMORY LAYOUT
 *
 * Heap is structured as blocks with metadata:
 *
 * ┌─────────────────────────────────────┐
 * │ BlockHeader (metadata)              │
 * │   - size (total size including header)
 * │   - is_free (1 = free, 0 = allocated)
 * │   - next pointer (for free list)
 * ├─────────────────────────────────────┤  ← User pointer returned from malloc
 * │ User Data                           │
 * │ (size - sizeof(BlockHeader))        │
 * └─────────────────────────────────────┘
 */

typedef struct BlockHeader {
    size_t size;              // Total size of block (including header)
    int is_free;              // 1 = free, 0 = allocated
    struct BlockHeader *next; // Free list chain
} BlockHeader;

#define ALIGNMENT 8
#define ALIGN(size) (((size) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))
#define HEADER_SIZE ALIGN(sizeof(BlockHeader))

// Global heap state
static void *heap_start = NULL;
static size_t heap_size = 0;
static BlockHeader *free_list = NULL;

// Statistics
static size_t total_allocated = 0;
static size_t total_freed = 0;
static size_t num_allocations = 0;
static size_t num_frees = 0;

/*
 * Initialize allocator with a large heap
 */
void allocator_init(size_t size) {
    heap_size = size;
    heap_start = malloc(size);
    
    if (!heap_start) {
        fprintf(stderr, "Failed to allocate heap\n");
        return;
    }
    
    // Initialize entire heap as one free block
    BlockHeader *header = (BlockHeader *)heap_start;
    header->size = size;
    header->is_free = 1;
    header->next = NULL;
    
    free_list = header;
    
    printf("Allocator initialized: %zu bytes\n", size);
}

/*
 * Find a suitable free block using first-fit strategy
 * Returns the block and its predecessor (for removal from list)
 */
static BlockHeader* find_free_block(size_t size, BlockHeader **prev) {
    BlockHeader *current = free_list;
    *prev = NULL;
    
    while (current) {
        if (current->is_free && current->size >= size) {
            return current;  // Found suitable block
        }
        *prev = current;
        current = current->next;
    }
    
    return NULL;  // No suitable block found
}

/*
 * Coalesce adjacent free blocks to reduce fragmentation
 * Called after freeing a block
 */
static void coalesce(void) {
    BlockHeader *current = free_list;
    
    while (current && current->next) {
        BlockHeader *next = current->next;
        
        // Are they adjacent in memory?
        // Current ends at: (char*)current + current->size
        // Next starts at: (char*)next
        
        if ((char *)current + current->size == (char *)next &&
            current->is_free && next->is_free) {
            
            // Merge blocks
            current->size += next->size;
            current->next = next->next;
            // Don't advance; check if we can merge with the new next
        } else {
            current = current->next;
        }
    }
}

/*
 * Allocate memory
 *
 * Algorithm:
 *   1. Find a free block large enough (first-fit)
 *   2. If block is larger: split it
 *   3. Mark block as allocated
 *   4. Return pointer to user data (after header)
 */
void* my_malloc(size_t size) {
    if (size == 0 || !heap_start) {
        return NULL;
    }
    
    // Align requested size
    size = ALIGN(size);
    
    // Total size needed: header + data
    size_t total_size = HEADER_SIZE + size;
    
    if (total_size > heap_size) {
        fprintf(stderr, "Allocation too large\n");
        return NULL;
    }
    
    // Find a free block
    BlockHeader *prev;
    BlockHeader *block = find_free_block(total_size, &prev);
    
    if (!block) {
        fprintf(stderr, "Out of memory\n");
        return NULL;
    }
    
    // If block is larger than needed, split it
    if (block->size > total_size) {
        BlockHeader *new_block = (BlockHeader *)((char *)block + total_size);
        new_block->size = block->size - total_size;
        new_block->is_free = 1;
        new_block->next = block->next;
        
        block->size = total_size;
        block->next = new_block;
    }
    
    // Mark as allocated and remove from free list
    block->is_free = 0;
    
    if (prev) {
        prev->next = block->next;
    } else {
        free_list = block->next;
    }
    
    block->next = NULL;
    
    // Update statistics
    total_allocated += total_size;
    num_allocations++;
    
    // Return pointer to user data (after header)
    return (char *)block + HEADER_SIZE;
}

/*
 * Free allocated memory
 *
 * Algorithm:
 *   1. Get block header (it's before the user pointer)
 *   2. Mark as free
 *   3. Add back to free list
 *   4. Coalesce adjacent free blocks
 */
void my_free(void *ptr) {
    if (!ptr) {
        return;
    }
    
    // Get header (just before user pointer)
    BlockHeader *block = (BlockHeader *)((char *)ptr - HEADER_SIZE);
    
    // Sanity check
    if (block->size == 0 || block->size > heap_size) {
        fprintf(stderr, "Invalid free: corrupted header\n");
        return;
    }
    
    // Mark as free
    block->is_free = 1;
    
    // Add back to free list (at beginning)
    block->next = free_list;
    free_list = block;
    
    // Update statistics
    total_freed += block->size;
    num_frees++;
    
    // Coalesce adjacent free blocks
    coalesce();
}

/*
 * Print allocator statistics
 */
void allocator_stats(void) {
    printf("\n=== Allocator Statistics ===\n");
    printf("Heap size:           %zu bytes\n", heap_size);
    printf("Total allocated:     %zu bytes\n", total_allocated);
    printf("Total freed:         %zu bytes\n", total_freed);
    printf("Allocations:         %zu\n", num_allocations);
    printf("Frees:               %zu\n", num_frees);
    printf("Net memory in use:   %zu bytes\n", total_allocated - total_freed);
    
    // Count free blocks
    int num_free_blocks = 0;
    size_t total_free_size = 0;
    BlockHeader *current = free_list;
    
    while (current) {
        num_free_blocks++;
        total_free_size += current->size;
        current = current->next;
    }
    
    printf("Free blocks:         %d\n", num_free_blocks);
    printf("Free memory:         %zu bytes\n", total_free_size);
    printf("Fragmentation:       %.1f%%\n", 
           (num_free_blocks > 1) ? (100.0 * (num_free_blocks - 1) / num_free_blocks) : 0.0);
}

/*
 * Cleanup allocator
 */
void allocator_cleanup(void) {
    if (heap_start) {
        free(heap_start);
        heap_start = NULL;
    }
}
