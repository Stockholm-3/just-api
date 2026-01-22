/**
 * file_cache.h - Unified file-based caching module
 *
 * Provides a generic caching API with TTL-based expiration,
 * MD5 key generation, and JSON support via jansson.
 */

#ifndef FILE_CACHE_H
#define FILE_CACHE_H

#include <stdbool.h>
#include <stddef.h>

#define FILE_CACHE_MAX_PATH_LENGTH 512
#define FILE_CACHE_KEY_LENGTH 33 /* MD5 hex string + null terminator */

/* Cache operation result codes */
typedef enum {
    FILE_CACHE_OK              = 0,
    FILE_CACHE_ERROR_PARAM     = -1, /* Invalid parameter */
    FILE_CACHE_ERROR_NOT_FOUND = -2, /* Cache entry not found */
    FILE_CACHE_ERROR_EXPIRED   = -3, /* Cache entry expired */
    FILE_CACHE_ERROR_IO        = -4, /* File I/O error */
    FILE_CACHE_ERROR_MEMORY    = -5, /* Memory allocation failed */
    FILE_CACHE_ERROR_HASH      = -6, /* Hash computation failed */
    FILE_CACHE_ERROR_PARSE     = -7  /* JSON parse error */
} FileCacheResult;

/* Configuration for a cache instance */
typedef struct {
    const char* cache_dir;   /* Directory for cache files */
    int         ttl_seconds; /* Time-to-live in seconds */
    bool        enabled;     /* Whether caching is enabled */
} FileCacheConfig;

/* Opaque cache instance handle */
typedef struct FileCacheInstance FileCacheInstance;

/* ============= Lifecycle ============= */

/**
 * Create and initialize a new cache instance.
 * Creates the cache directory if it doesn't exist.
 *
 * @param config  Configuration for this cache instance
 * @return        Pointer to cache instance, or NULL on error
 */
FileCacheInstance* file_cache_create(const FileCacheConfig* config);

/**
 * Destroy a cache instance and free resources.
 *
 * @param cache  Cache instance to destroy
 */
void file_cache_destroy(FileCacheInstance* cache);

/* ============= Core Operations ============= */

/**
 * Generate a cache key (MD5 hash) from input string.
 *
 * @param cache     Cache instance
 * @param input     Input string to hash
 * @param out_key   Output buffer for hex hash (>= FILE_CACHE_KEY_LENGTH)
 * @param key_size  Size of out_key buffer
 * @return          FILE_CACHE_OK on success, error code otherwise
 */
FileCacheResult file_cache_generate_key(FileCacheInstance* cache,
                                        const char* input, char* out_key,
                                        size_t key_size);

/**
 * Check if a cache entry exists and is not expired.
 *
 * @param cache      Cache instance
 * @param cache_key  The cache key (MD5 hash from file_cache_generate_key)
 * @return           true if valid cache entry exists, false otherwise
 */
bool file_cache_is_valid(FileCacheInstance* cache, const char* cache_key);

/**
 * Load raw data from cache file.
 * Checks TTL before loading. Returns FILE_CACHE_ERROR_EXPIRED if entry stale.
 *
 * @param cache      Cache instance
 * @param cache_key  The cache key
 * @param out_data   Output pointer for loaded data (caller must free)
 * @param out_size   Output size of loaded data (optional, can be NULL)
 * @return           FILE_CACHE_OK on success, error code otherwise
 */
FileCacheResult file_cache_load(FileCacheInstance* cache, const char* cache_key,
                                char** out_data, size_t* out_size);

/**
 * Save raw data to cache file.
 *
 * @param cache      Cache instance
 * @param cache_key  The cache key
 * @param data       Data to save
 * @param data_size  Size of data in bytes (0 = use strlen)
 * @return           FILE_CACHE_OK on success, error code otherwise
 */
FileCacheResult file_cache_save(FileCacheInstance* cache, const char* cache_key,
                                const char* data, size_t data_size);

/* ============= JSON Helpers ============= */

/**
 * Load and parse JSON from cache (convenience wrapper).
 *
 * @param cache      Cache instance
 * @param cache_key  The cache key
 * @param out_json   Output pointer for parsed JSON (jansson json_t*)
 *                   Caller must call json_decref() when done.
 * @return           FILE_CACHE_OK on success, error code otherwise
 */
FileCacheResult file_cache_load_json(FileCacheInstance* cache,
                                     const char* cache_key, void** out_json);

/**
 * Serialize and save JSON to cache (convenience wrapper).
 *
 * @param cache      Cache instance
 * @param cache_key  The cache key
 * @param json       JSON object to save (jansson json_t*)
 * @return           FILE_CACHE_OK on success, error code otherwise
 */
FileCacheResult file_cache_save_json(FileCacheInstance* cache,
                                     const char* cache_key, void* json);

/* ============= Cache Management ============= */

/**
 * Invalidate (delete) a specific cache entry.
 *
 * @param cache      Cache instance
 * @param cache_key  The cache key
 * @return           FILE_CACHE_OK on success, error code otherwise
 */
FileCacheResult file_cache_invalidate(FileCacheInstance* cache,
                                      const char*        cache_key);

/**
 * Clear all cache entries for this cache instance.
 *
 * @param cache  Cache instance
 * @return       FILE_CACHE_OK on success, error code otherwise
 */
FileCacheResult file_cache_clear(FileCacheInstance* cache);

/* ============= Utilities ============= */

/**
 * Normalize a string for use as cache key input.
 * Converts to lowercase, collapses whitespace to underscores,
 * and trims leading/trailing underscores.
 *
 * @param input        Input string
 * @param output       Output buffer
 * @param output_size  Size of output buffer
 * @return             FILE_CACHE_OK on success, error code otherwise
 */
FileCacheResult file_cache_normalize_string(const char* input, char* output,
                                            size_t output_size);

/**
 * Get the full filepath for a cache entry.
 *
 * @param cache      Cache instance
 * @param cache_key  The cache key
 * @param out_path   Output buffer for filepath
 * @param path_size  Size of out_path buffer
 * @return           FILE_CACHE_OK on success, error code otherwise
 */
FileCacheResult file_cache_get_filepath(FileCacheInstance* cache,
                                        const char* cache_key, char* out_path,
                                        size_t path_size);

#endif /* FILE_CACHE_H */
