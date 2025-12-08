#ifndef CACHE_H
#define CACHE_H

#include "linked_list.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

// Cache entry structure
typedef struct {
    char*  key;       // Cache key (e.g., request URL or identifier)
    void*  data;      // Cached data
    size_t data_size; // Size of cached data
    time_t timestamp; // When the entry was cached
    time_t expiry;    // When the entry should expire
} CacheEntry;

// Cache structure
typedef struct {
    LinkedList* entries;     // List of cache entries
    size_t      max_size;    // Maximum number of entries
    time_t      default_ttl; // Default time-to-live in seconds
} Cache;

// Function declarations
Cache* cache_create(size_t max_size, time_t default_ttl);
void   cache_destroy(Cache* cache);
int    cache_set(Cache* cache, const char* key, void* data, size_t data_size,
                 time_t ttl);
void*  cache_get(Cache* cache, const char* key, size_t* data_size);
void   cache_remove(Cache* cache, const char* key);
void   cache_clear(Cache* cache);

#endif /* CACHE_H */