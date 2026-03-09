/**
 * @file cache_cleaner.h
 * @brief Automatic cache cleanup utility for expired JSON cache files.
 *
 * This module provides scheduled cleanup of expired cache files with the
 * following features:
 * - Automatic cleanup at server startup
 * - Daily cleanup triggered at midnight (00:00)
 * - Recursive directory traversal for nested cache structures
 * - Configurable TTL (Time-To-Live) per cache directory
 *
 * @section usage Usage Example
 * @code{.c}
 * CacheCleanerConfig config = {
 *     .entries = {
 *         {.cache_dir = "./cache/weather", .ttl_seconds = 900},
 *         {.cache_dir = "./cache/geo", .ttl_seconds = 604800}
 *     },
 *     .entry_count = 2,
 *     .verbose = true
 * };
 *
 * CacheCleaner* cleaner = cache_cleaner_create(&config);
 * // ... server runs ...
 * cache_cleaner_destroy(cleaner);
 * @endcode
 *
 * @author Oleksandr
 * @date 2025
 */

#ifndef CACHE_CLEANER_H
#define CACHE_CLEANER_H

#include <stdbool.h>

/**
 * @defgroup cache_cleaner Cache Cleaner Module
 * @{
 */

/** @brief Maximum number of cache directories that can be configured. */
#define CACHE_CLEANER_MAX_DIRS 8

/** @brief Maximum path length for cache file paths. */
#define CACHE_CLEANER_MAX_PATH 512

/**
 * @brief Configuration for a single cache directory.
 *
 * Defines the path and TTL settings for one cache directory
 * that the cleaner will monitor.
 */
typedef struct {
    const char* cache_dir;   /**< Path to the cache directory. */
    int         ttl_seconds; /**< Time-to-live in seconds for cached files. */
} CacheCleanerEntry;

/**
 * @brief Configuration for the cache cleaner module.
 *
 * Contains all settings needed to initialize the cache cleaner,
 * including the list of directories to monitor and logging preferences.
 */
typedef struct {
    CacheCleanerEntry
        entries[CACHE_CLEANER_MAX_DIRS]; /**< Array of cache directory
                                            configurations. */
    int  entry_count; /**< Number of configured directories. */
    bool verbose;     /**< Enable verbose logging of deleted files. */
} CacheCleanerConfig;

/**
 * @brief Opaque handle for the cache cleaner instance.
 *
 * The internal structure is hidden to provide encapsulation.
 * Use cache_cleaner_create() and cache_cleaner_destroy() for lifecycle
 * management.
 */
typedef struct CacheCleaner CacheCleaner;

/* ============= Lifecycle Functions ============= */

/**
 * @brief Create and initialize the cache cleaner.
 *
 * This function performs the following:
 * 1. Validates the configuration
 * 2. Allocates internal structures
 * 3. Registers an SMW task for daily midnight cleanup checks
 * 4. Performs an immediate cleanup of all configured directories
 *
 * @param[in] config Pointer to the cleaner configuration.
 *                   Must not be NULL and entry_count must be > 0.
 *
 * @return Pointer to the created CacheCleaner instance on success.
 * @retval NULL on error (invalid config, memory allocation failure,
 *         or SMW task registration failure).
 *
 * @note The cleaner will immediately run cleanup on all directories
 *       during creation to ensure a clean state at startup.
 *
 * @see cache_cleaner_destroy()
 */
CacheCleaner* cache_cleaner_create(const CacheCleanerConfig* config);

/**
 * @brief Destroy the cache cleaner and release all resources.
 *
 * This function:
 * 1. Unregisters the SMW task
 * 2. Frees all allocated memory
 *
 * @param[in] cleaner Pointer to the CacheCleaner instance to destroy.
 *                    Safe to call with NULL (no operation).
 *
 * @see cache_cleaner_create()
 */
void cache_cleaner_destroy(CacheCleaner* cleaner);

/* ============= Operations ============= */

/**
 * @brief Run cleanup on all configured cache directories.
 *
 * Iterates through all configured directories and removes
 * expired .json files based on their TTL settings.
 * This function is called automatically:
 * - Once at server startup (during cache_cleaner_create())
 * - Daily at midnight (via SMW task)
 *
 * Can also be called manually to force an immediate cleanup.
 *
 * @param[in] cleaner Pointer to the CacheCleaner instance.
 *
 * @return Total number of files deleted across all directories.
 * @retval -1 if cleaner is NULL.
 *
 * @see cache_cleaner_clean_directory()
 */
int cache_cleaner_run(CacheCleaner* cleaner);

/**
 * @brief Clean expired files from a specific directory recursively.
 *
 * Traverses the specified directory and all subdirectories,
 * removing .json files that exceed the specified TTL.
 *
 * File age is determined by comparing the file's modification time
 * (st_mtime) against the current time.
 *
 * @param[in] cache_dir    Path to the cache directory to clean.
 * @param[in] ttl_seconds  TTL threshold in seconds. Files older than
 *                         this value will be deleted.
 * @param[in] verbose      If true, log each deleted file to stdout.
 *
 * @return Number of files successfully deleted.
 * @retval -1 if cache_dir is NULL or ttl_seconds <= 0.
 * @retval 0 if directory doesn't exist or contains no expired files.
 *
 * @note Only files with .json extension are considered for deletion.
 * @note Subdirectories are traversed recursively but not deleted.
 */
int cache_cleaner_clean_directory(const char* cache_dir, int ttl_seconds,
                                  bool verbose);

/** @} */ /* End of cache_cleaner group */

#endif /* CACHE_CLEANER_H */
