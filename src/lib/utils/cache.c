#include "cache.h"

#include <stdio.h>

// Helper function to free a cache entry
static void free_cache_entry(CacheEntry* entry) {
    if (entry) {
        free(entry->key);
        free(entry->data);
        free(entry);
    }
}

// Helper function to check if an entry is expired
static int is_expired(CacheEntry* entry) {
    if (!entry) {
        return 1;
    }
    return (time(NULL) > entry->expiry);
}

Cache* cache_create(size_t max_size, time_t default_ttl) {
    Cache* cache = (Cache*)malloc(sizeof(Cache));
    if (!cache) {
        return NULL;
    }

    cache->entries = linked_list_create();
    if (!cache->entries) {
        free(cache);
        return NULL;
    }

    cache->max_size    = max_size;
    cache->default_ttl = default_ttl;
    return cache;
}

void cache_destroy(Cache* cache) {
    if (!cache) {
        return;
    }
    linked_list_clear(cache->entries, (void (*)(void*))free_cache_entry);
    linked_list_dispose(&cache->entries, NULL);
    free(cache);
}

int cache_set(Cache* cache, const char* key, void* data, size_t data_size,
              time_t ttl) {
    if (!cache || !key || !data) {
        return -1;
    }

    // Remove existing entry if it exists
    cache_remove(cache, key);

    // Check if cache is full
    if (cache->entries->size >= cache->max_size) {
        // Remove oldest entry (first in list)
        if (cache->entries->head) {
            CacheEntry* oldest = (CacheEntry*)cache->entries->head->item;
            cache_remove(cache, oldest->key);
        }
    }

    // Create new entry
    CacheEntry* entry = (CacheEntry*)malloc(sizeof(CacheEntry));
    if (!entry) {
        return -1;
    }

    entry->key  = strdup(key);
    entry->data = malloc(data_size);
    if (!entry->key || !entry->data) {
        free_cache_entry(entry);
        return -1;
    }

    memcpy(entry->data, data, data_size);
    entry->data_size = data_size;
    entry->timestamp = time(NULL);
    entry->expiry    = entry->timestamp + (ttl > 0 ? ttl : cache->default_ttl);

    // Add to list
    if (linked_list_append(cache->entries, entry) != 0) {
        free_cache_entry(entry);
        return -1;
    }

    return 0;
}

void* cache_get(Cache* cache, const char* key, size_t* data_size) {
    if (!cache || !key) {
        return NULL;
    }

    LinkedList_foreach(cache->entries, node) {
        CacheEntry* entry = (CacheEntry*)node->item;
        if (strcmp(entry->key, key) == 0) {
            if (is_expired(entry)) {
                cache_remove(cache, key);
                return NULL;
            }
            if (data_size) {
                *data_size = entry->data_size;
            }
            void* data_copy = malloc(entry->data_size);
            if (data_copy) {
                memcpy(data_copy, entry->data, entry->data_size);
            }
            return data_copy;
        }
    }

    return NULL;
}

void cache_remove(Cache* cache, const char* key) {
    if (!cache || !key) {
        return;
    }

    size_t index = 0;
    LinkedList_foreach(cache->entries, node) {
        CacheEntry* entry = (CacheEntry*)node->item;
        if (strcmp(entry->key, key) == 0) {
            linked_list_pop(cache->entries, index,
                            (void (*)(void*))free_cache_entry);
            return;
        }
        index++;
    }
}

void cache_clear(Cache* cache) {
    if (!cache) {
        return;
    }
    linked_list_clear(cache->entries, (void (*)(void*))free_cache_entry);
}