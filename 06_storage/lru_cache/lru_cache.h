#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

typedef struct lru_cache lru_cache_t;

/**
 * Create an LRU cache with max_size entries.
 * Returns NULL on failure.
 */
lru_cache_t *lru_cache_create(int max_size);

/**
 * Get value from cache.
 * Returns value if found, or NULL if not found.
 * Marks entry as most recently used.
 */
void *lru_cache_get(lru_cache_t *cache, int key);

/**
 * Put key-value pair into cache.
 * If key exists, updates value and marks as MRU.
 * If cache is full, evicts LRU entry.
 * Returns 0 on success, -1 on allocation failure.
 */
int lru_cache_put(lru_cache_t *cache, int key, void *value);

/**
 * Remove all entries from cache.
 */
void lru_cache_clear(lru_cache_t *cache);

/**
 * Get cache statistics.
 * hits: number of successful gets
 * misses: number of failed gets
 * evictions: number of entries evicted
 */
void lru_cache_stats(lru_cache_t *cache, int *hits, int *misses, int *evictions);

/**
 * Destroy cache and free all memory.
 */
void lru_cache_destroy(lru_cache_t *cache);

#endif
