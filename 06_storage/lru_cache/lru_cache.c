#include "lru_cache.h"
#include <stdlib.h>
#include <string.h>

#define HASH_TABLE_SIZE 1024

typedef struct node {
    int key;
    void *value;
    struct node *prev;
    struct node *next;
} node_t;

struct lru_cache {
    node_t *head;  // MRU (most recently used)
    node_t *tail;  // LRU (least recently used)
    int max_size;
    int current_size;
    
    // Hash table for O(1) lookup
    node_t **table;
    
    // Statistics
    int hits;
    int misses;
    int evictions;
    
    // Synchronization
    pthread_mutex_t lock;
};

static int hash_key(int key) {
    return key % HASH_TABLE_SIZE;
}

static node_t *find_node(lru_cache_t *cache, int key) {
    int hash = hash_key(key);
    node_t *node = cache->table[hash];
    
    while (node) {
        if (node->key == key) {
            return node;
        }
        node = node->next;
    }
    
    return NULL;
}

static void move_to_front(lru_cache_t *cache, node_t *node) {
    // Remove from current position
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        // Already at head
        return;
    }
    
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        cache->tail = node->prev;
    }
    
    // Add to front (MRU)
    node->prev = NULL;
    node->next = cache->head;
    if (cache->head) {
        cache->head->prev = node;
    }
    cache->head = node;
    
    if (!cache->tail) {
        cache->tail = node;
    }
}

static void evict_lru(lru_cache_t *cache) {
    if (!cache->tail) {
        return;
    }
    
    node_t *victim = cache->tail;
    
    // Remove from list
    if (victim->prev) {
        victim->prev->next = NULL;
    } else {
        cache->head = NULL;
    }
    cache->tail = victim->prev;
    
    // Remove from hash table
    int hash = hash_key(victim->key);
    node_t *node = cache->table[hash];
    
    if (node == victim) {
        cache->table[hash] = node->next;
    } else {
        while (node && node->next != victim) {
            node = node->next;
        }
        if (node) {
            node->next = victim->next;
        }
    }
    
    cache->current_size--;
    cache->evictions++;
    free(victim);
}

lru_cache_t *lru_cache_create(int max_size) {
    if (max_size <= 0) {
        return NULL;
    }
    
    lru_cache_t *cache = malloc(sizeof(lru_cache_t));
    if (!cache) {
        return NULL;
    }
    
    cache->table = malloc(HASH_TABLE_SIZE * sizeof(node_t *));
    if (!cache->table) {
        free(cache);
        return NULL;
    }
    
    memset(cache->table, 0, HASH_TABLE_SIZE * sizeof(node_t *));
    
    cache->head = NULL;
    cache->tail = NULL;
    cache->max_size = max_size;
    cache->current_size = 0;
    cache->hits = 0;
    cache->misses = 0;
    cache->evictions = 0;
    
    if (pthread_mutex_init(&cache->lock, NULL) != 0) {
        free(cache->table);
        free(cache);
        return NULL;
    }
    
    return cache;
}

void *lru_cache_get(lru_cache_t *cache, int key) {
    if (!cache) {
        return NULL;
    }
    
    pthread_mutex_lock(&cache->lock);
    
    node_t *node = find_node(cache, key);
    if (!node) {
        cache->misses++;
        pthread_mutex_unlock(&cache->lock);
        return NULL;
    }
    
    // Move to front (mark as MRU)
    move_to_front(cache, node);
    cache->hits++;
    
    void *value = node->value;
    pthread_mutex_unlock(&cache->lock);
    
    return value;
}

int lru_cache_put(lru_cache_t *cache, int key, void *value) {
    if (!cache) {
        return -1;
    }
    
    pthread_mutex_lock(&cache->lock);
    
    // Check if key already exists
    node_t *existing = find_node(cache, key);
    if (existing) {
        existing->value = value;
        move_to_front(cache, existing);
        pthread_mutex_unlock(&cache->lock);
        return 0;
    }
    
    // New entry
    node_t *new_node = malloc(sizeof(node_t));
    if (!new_node) {
        pthread_mutex_unlock(&cache->lock);
        return -1;
    }
    
    new_node->key = key;
    new_node->value = value;
    new_node->prev = NULL;
    new_node->next = cache->head;
    
    if (cache->head) {
        cache->head->prev = new_node;
    }
    cache->head = new_node;
    
    if (!cache->tail) {
        cache->tail = new_node;
    }
    
    // Add to hash table
    int hash = hash_key(key);
    new_node->next = cache->table[hash];
    cache->table[hash] = new_node;
    
    cache->current_size++;
    
    // Evict if over capacity
    if (cache->current_size > cache->max_size) {
        evict_lru(cache);
    }
    
    pthread_mutex_unlock(&cache->lock);
    return 0;
}

void lru_cache_clear(lru_cache_t *cache) {
    if (!cache) {
        return;
    }
    
    pthread_mutex_lock(&cache->lock);
    
    node_t *node = cache->head;
    while (node) {
        node_t *next = node->next;
        free(node);
        node = next;
    }
    
    memset(cache->table, 0, HASH_TABLE_SIZE * sizeof(node_t *));
    cache->head = NULL;
    cache->tail = NULL;
    cache->current_size = 0;
    
    pthread_mutex_unlock(&cache->lock);
}

void lru_cache_stats(lru_cache_t *cache, int *hits, int *misses, int *evictions) {
    if (!cache) {
        return;
    }
    
    pthread_mutex_lock(&cache->lock);
    if (hits) *hits = cache->hits;
    if (misses) *misses = cache->misses;
    if (evictions) *evictions = cache->evictions;
    pthread_mutex_unlock(&cache->lock);
}

void lru_cache_destroy(lru_cache_t *cache) {
    if (!cache) {
        return;
    }
    
    lru_cache_clear(cache);
    pthread_mutex_destroy(&cache->lock);
    free(cache->table);
    free(cache);
}
