/**
 * @file cache_cleaner.c
 * @brief Implementation of the automatic cache cleanup utility.
 *
 * This file contains the implementation of the cache cleaner module,
 * including the SMW task callback for midnight detection and the
 * recursive directory traversal algorithm.
 *
 * @see cache_cleaner.h
 */

#include "cache_cleaner.h"

#include "smw.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ============= Internal Structure ============= */

/**
 * @brief Internal structure for the cache cleaner instance.
 *
 * Contains the configuration, SMW task handle, and state
 * for tracking daily cleanup execution.
 */
struct CacheCleaner {
    CacheCleanerConfig config; /**< Copy of the user configuration. */
    SmwTask*           task;   /**< Handle to the registered SMW task. */
    int last_cleanup_day;      /**< Day of year (0-365) when last cleanup ran.
                                    Initialized to -1 to force startup cleanup. */
};

/* ============= Internal Helper Functions ============= */

/**
 * @brief Get the current day of year.
 *
 * Uses localtime() to get the day of year from the system clock,
 * which respects the system timezone settings.
 *
 * @return Day of year (0-365, where 0 = January 1st).
 *
 * @note This function is used for midnight detection by comparing
 *       consecutive calls. When the return value changes, midnight
 *       has passed.
 */
static int get_day_of_year(void) {
    time_t     now     = time(NULL);
    struct tm* tm_info = localtime(&now);
    return tm_info->tm_yday;
}

/**
 * @brief Recursively clean a directory and all its subdirectories.
 *
 * Internal implementation of the recursive directory traversal.
 * Processes all entries in the directory:
 * - Directories: recurses into them
 * - Regular .json files: checks age and deletes if expired
 * - Other files: ignored
 *
 * @param[in] dir         Path to the directory to clean.
 * @param[in] ttl_seconds TTL threshold in seconds.
 * @param[in] verbose     Enable verbose logging.
 * @param[in] now         Current timestamp (passed to avoid repeated time()
 * calls).
 *
 * @return Number of files deleted in this directory and all subdirectories.
 *
 * @note Uses POSIX opendir/readdir/closedir for portability.
 * @note Files with paths exceeding CACHE_CLEANER_MAX_PATH are skipped.
 */
static int clean_directory_recursive(const char* dir, int ttl_seconds,
                                     bool verbose, time_t now) {
    DIR* d = opendir(dir);
    if (!d) {
        return 0;
    }

    struct dirent* entry;
    struct stat    st;
    int            deleted = 0;
    char           filepath[CACHE_CLEANER_MAX_PATH];

    while ((entry = readdir(d)) != NULL) {
        /* Skip . and .. to prevent infinite recursion */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Build full path */
        int written =
            snprintf(filepath, sizeof(filepath), "%s/%s", dir, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(filepath)) {
            continue; /* Path too long, skip */
        }

        if (stat(filepath, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            /* Recursive call for subdirectory */
            deleted +=
                clean_directory_recursive(filepath, ttl_seconds, verbose, now);
        } else if (S_ISREG(st.st_mode)) {
            /* Check if it's a .json file */
            size_t name_len = strlen(entry->d_name);
            if (name_len > 5 &&
                strcmp(entry->d_name + name_len - 5, ".json") == 0) {
                double age = difftime(now, st.st_mtime);
                if (age > ttl_seconds) {
                    if (unlink(filepath) == 0) {
                        deleted++;
                        if (verbose) {
                            printf(
                                "[CACHE_CLEANER] Deleted: %s (age: %.0f sec)\n",
                                filepath, age);
                        }
                    }
                }
            }
        }
    }

    closedir(d);
    return deleted;
}

/**
 * @brief SMW task callback for periodic midnight detection.
 *
 * This callback is invoked on every iteration of the main server loop.
 * It performs a lightweight check to detect if midnight has passed
 * by comparing the current day of year with the last recorded day.
 *
 * When a day change is detected (including year rollover from day 365 to 0),
 * the cleanup is triggered automatically.
 *
 * @param[in] context  Pointer to the CacheCleaner instance.
 * @param[in] mon_time Monotonic time in milliseconds (unused).
 *
 * @note The check is O(1) - only compares two integers per iteration.
 * @note Handles year transitions correctly (day 365 -> day 0).
 */
static void cache_cleaner_task_callback(void* context, uint64_t mon_time) {
    (void)mon_time;

    CacheCleaner* cleaner = (CacheCleaner*)context;

    int current_day = get_day_of_year();

    /* Check if we crossed midnight (day changed) */
    if (cleaner->last_cleanup_day != current_day) {
        int deleted = cache_cleaner_run(cleaner);

        if (cleaner->config.verbose) {
            printf("[CACHE_CLEANER] Daily cleanup: %d files deleted\n",
                   deleted);
        }

        cleaner->last_cleanup_day = current_day;
    }
}

/* ============= Public API Implementation ============= */

int cache_cleaner_clean_directory(const char* cache_dir, int ttl_seconds,
                                  bool verbose) {
    if (!cache_dir || ttl_seconds <= 0) {
        return -1;
    }

    time_t now = time(NULL);
    return clean_directory_recursive(cache_dir, ttl_seconds, verbose, now);
}

int cache_cleaner_run(CacheCleaner* cleaner) {
    if (!cleaner) {
        return -1;
    }

    int total_deleted = 0;

    for (int i = 0; i < cleaner->config.entry_count; i++) {
        CacheCleanerEntry* entry = &cleaner->config.entries[i];

        int deleted = cache_cleaner_clean_directory(
            entry->cache_dir, entry->ttl_seconds, cleaner->config.verbose);

        if (deleted > 0) {
            total_deleted += deleted;
        }
    }

    return total_deleted;
}

CacheCleaner* cache_cleaner_create(const CacheCleanerConfig* config) {
    if (!config || config->entry_count <= 0 ||
        config->entry_count > CACHE_CLEANER_MAX_DIRS) {
        return NULL;
    }

    CacheCleaner* cleaner = calloc(1, sizeof(CacheCleaner));
    if (!cleaner) {
        return NULL;
    }

    /* Copy configuration */
    cleaner->config = *config;

    /* Initialize state - use -1 to force immediate cleanup on first SMW tick */
    cleaner->last_cleanup_day = -1;

    /* Register SMW task for midnight checks */
    cleaner->task = smw_create_task(cleaner, cache_cleaner_task_callback);
    if (!cleaner->task) {
        free(cleaner);
        return NULL;
    }

    printf("[CACHE_CLEANER] Initialized with %d cache directories\n",
           config->entry_count);

    /* Run initial cleanup at startup */
    int deleted = cache_cleaner_run(cleaner);
    printf("[CACHE_CLEANER] Startup cleanup: %d files deleted\n", deleted);

    /* Update last cleanup day so we don't run again until midnight */
    cleaner->last_cleanup_day = get_day_of_year();

    return cleaner;
}

void cache_cleaner_destroy(CacheCleaner* cleaner) {
    if (!cleaner) {
        return;
    }

    if (cleaner->task) {
        smw_destroy_task(cleaner->task);
    }

    free(cleaner);
    printf("[CACHE_CLEANER] Destroyed\n");
}
