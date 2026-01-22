/**
 * file_cache.c - Unified file-based caching module implementation
 */

#include "file_cache.h"

#include <dirent.h>
#include <errno.h>
#include <hash_md5.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ============= Internal Structure ============= */

struct FileCacheInstance {
    char cache_dir[FILE_CACHE_MAX_PATH_LENGTH];
    int  ttl_seconds;
    bool enabled;
};

/* ============= Internal Helpers ============= */

/**
 * Create directory and all parent directories (like mkdir -p)
 */
static int mkdir_recursive(const char* path, mode_t mode) {
    char   tmp[FILE_CACHE_MAX_PATH_LENGTH];
    char*  p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (len == 0) {
        return -1;
    }

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';

            if (mkdir(tmp, mode) != 0) {
                if (errno != EEXIST) {
                    return -1;
                }
            }

            *p = '/';
        }
    }

    if (mkdir(tmp, mode) != 0) {
        if (errno != EEXIST) {
            return -1;
        }
    }

    return 0;
}

/**
 * Build full filepath for cache entry
 */
static void build_filepath(const FileCacheInstance* cache,
                           const char* cache_key, char* out_path,
                           size_t path_size) {
    snprintf(out_path, path_size, "%s/%s.json", cache->cache_dir, cache_key);
}

/**
 * Check if file exists and is within TTL
 */
static bool is_file_valid(const char* filepath, int ttl_seconds) {
    struct stat file_stat;

    if (stat(filepath, &file_stat) != 0) {
        return false; /* File does not exist */
    }

    time_t now = time(NULL);
    double age = difftime(now, file_stat.st_mtime);

    return (age <= ttl_seconds);
}

/* ============= Lifecycle Implementation ============= */

FileCacheInstance* file_cache_create(const FileCacheConfig* config) {
    if (!config || !config->cache_dir) {
        return NULL;
    }

    FileCacheInstance* cache = calloc(1, sizeof(FileCacheInstance));
    if (!cache) {
        return NULL;
    }

    strncpy(cache->cache_dir, config->cache_dir, sizeof(cache->cache_dir) - 1);
    cache->ttl_seconds = config->ttl_seconds;
    cache->enabled     = config->enabled;

    /* Create cache directory if it doesn't exist */
    if (mkdir_recursive(cache->cache_dir, 0755) != 0) {
        fprintf(stderr,
                "[FILE_CACHE] Warning: Failed to create cache directory: %s\n",
                cache->cache_dir);
    }

    return cache;
}

void file_cache_destroy(FileCacheInstance* cache) {
    if (cache) {
        free(cache);
    }
}

/* ============= Core Operations Implementation ============= */

FileCacheResult file_cache_generate_key(FileCacheInstance* cache,
                                        const char* input, char* out_key,
                                        size_t key_size) {
    (void)cache; /* Cache instance not needed for key generation */

    if (!input || !out_key || key_size < FILE_CACHE_KEY_LENGTH) {
        return FILE_CACHE_ERROR_PARAM;
    }

    if (hash_md5_string(input, strlen(input), out_key, key_size) != 0) {
        return FILE_CACHE_ERROR_HASH;
    }

    return FILE_CACHE_OK;
}

bool file_cache_is_valid(FileCacheInstance* cache, const char* cache_key) {
    if (!cache || !cache->enabled || !cache_key) {
        return false;
    }

    char filepath[FILE_CACHE_MAX_PATH_LENGTH];
    build_filepath(cache, cache_key, filepath, sizeof(filepath));

    return is_file_valid(filepath, cache->ttl_seconds);
}

FileCacheResult file_cache_load(FileCacheInstance* cache, const char* cache_key,
                                char** out_data, size_t* out_size) {
    if (!cache || !cache_key || !out_data) {
        return FILE_CACHE_ERROR_PARAM;
    }

    if (!cache->enabled) {
        return FILE_CACHE_ERROR_NOT_FOUND;
    }

    char filepath[FILE_CACHE_MAX_PATH_LENGTH];
    build_filepath(cache, cache_key, filepath, sizeof(filepath));

    /* Check TTL */
    if (!is_file_valid(filepath, cache->ttl_seconds)) {
        return FILE_CACHE_ERROR_EXPIRED;
    }

    /* Open and read file */
    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        return FILE_CACHE_ERROR_NOT_FOUND;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(fp);
        return FILE_CACHE_ERROR_IO;
    }

    /* Allocate buffer */
    char* buffer = malloc((size_t)file_size + 1);
    if (!buffer) {
        fclose(fp);
        return FILE_CACHE_ERROR_MEMORY;
    }

    /* Read content */
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        free(buffer);
        return FILE_CACHE_ERROR_IO;
    }

    buffer[bytes_read] = '\0';

    *out_data = buffer;
    if (out_size) {
        *out_size = bytes_read;
    }

    return FILE_CACHE_OK;
}

FileCacheResult file_cache_save(FileCacheInstance* cache, const char* cache_key,
                                const char* data, size_t data_size) {
    if (!cache || !cache_key || !data) {
        return FILE_CACHE_ERROR_PARAM;
    }

    if (!cache->enabled) {
        return FILE_CACHE_OK; /* Silently succeed when disabled */
    }

    char filepath[FILE_CACHE_MAX_PATH_LENGTH];
    build_filepath(cache, cache_key, filepath, sizeof(filepath));

    if (data_size == 0) {
        data_size = strlen(data);
    }

    FILE* fp = fopen(filepath, "w");
    if (!fp) {
        return FILE_CACHE_ERROR_IO;
    }

    size_t bytes_written = fwrite(data, 1, data_size, fp);
    fclose(fp);

    if (bytes_written != data_size) {
        return FILE_CACHE_ERROR_IO;
    }

    return FILE_CACHE_OK;
}

/* ============= JSON Helpers Implementation ============= */

FileCacheResult file_cache_load_json(FileCacheInstance* cache,
                                     const char* cache_key, void** out_json) {
    if (!out_json) {
        return FILE_CACHE_ERROR_PARAM;
    }

    char*  data = NULL;
    size_t size = 0;

    FileCacheResult result = file_cache_load(cache, cache_key, &data, &size);
    if (result != FILE_CACHE_OK) {
        return result;
    }

    json_error_t error;
    json_t*      json = json_loadb(data, size, 0, &error);
    free(data);

    if (!json) {
        return FILE_CACHE_ERROR_PARSE;
    }

    *out_json = json;
    return FILE_CACHE_OK;
}

FileCacheResult file_cache_save_json(FileCacheInstance* cache,
                                     const char* cache_key, void* json) {
    if (!json) {
        return FILE_CACHE_ERROR_PARAM;
    }

    char* json_str =
        json_dumps((json_t*)json, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
    if (!json_str) {
        return FILE_CACHE_ERROR_MEMORY;
    }

    FileCacheResult result = file_cache_save(cache, cache_key, json_str, 0);
    free(json_str);

    return result;
}

/* ============= Cache Management Implementation ============= */

FileCacheResult file_cache_invalidate(FileCacheInstance* cache,
                                      const char*        cache_key) {
    if (!cache || !cache_key) {
        return FILE_CACHE_ERROR_PARAM;
    }

    char filepath[FILE_CACHE_MAX_PATH_LENGTH];
    build_filepath(cache, cache_key, filepath, sizeof(filepath));

    if (unlink(filepath) != 0 && errno != ENOENT) {
        return FILE_CACHE_ERROR_IO;
    }

    return FILE_CACHE_OK;
}

FileCacheResult file_cache_clear(FileCacheInstance* cache) {
    if (!cache) {
        return FILE_CACHE_ERROR_PARAM;
    }

    DIR* dir = opendir(cache->cache_dir);
    if (!dir) {
        if (errno == ENOENT) {
            return FILE_CACHE_OK; /* Directory doesn't exist, nothing to clear
                                   */
        }
        return FILE_CACHE_ERROR_IO;
    }

    struct dirent* entry;
    char           filepath[FILE_CACHE_MAX_PATH_LENGTH];
    int            errors = 0;

    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Only delete .json files */
        size_t name_len = strlen(entry->d_name);
        if (name_len > 5 &&
            strcmp(entry->d_name + name_len - 5, ".json") == 0) {
            int written = snprintf(filepath, sizeof(filepath), "%s/%s",
                                   cache->cache_dir, entry->d_name);

            if (written < 0 || (size_t)written >= sizeof(filepath)) {
                errors++;
                continue; /* Path too long, skip */
            }

            if (unlink(filepath) != 0) {
                errors++;
            }
        }
    }

    closedir(dir);

    return (errors == 0) ? FILE_CACHE_OK : FILE_CACHE_ERROR_IO;
}

/* ============= Utilities Implementation ============= */

FileCacheResult file_cache_normalize_string(const char* input, char* output,
                                            size_t output_size) {
    if (!input || !output || output_size == 0) {
        return FILE_CACHE_ERROR_PARAM;
    }

    size_t j            = 0;
    int    prev_was_sep = 0;

    for (size_t i = 0; input[i] != '\0' && j + 1 < output_size; ++i) {
        unsigned char c = (unsigned char)input[i];

        if (c == ' ' || c == '\t' || c == '+' || c == '_') {
            if (j == 0 || prev_was_sep) {
                continue; /* Skip leading or consecutive separators */
            }
            output[j++]  = '_';
            prev_was_sep = 1;
        } else {
            /* ASCII-only lowercase conversion */
            if (c >= 'A' && c <= 'Z') {
                output[j++] = (char)(c - 'A' + 'a');
            } else {
                output[j++] = (char)c;
            }
            prev_was_sep = 0;
        }
    }

    /* Trim trailing underscore */
    if (j > 0 && output[j - 1] == '_') {
        j--;
    }

    output[j] = '\0';

    return FILE_CACHE_OK;
}

FileCacheResult file_cache_get_filepath(FileCacheInstance* cache,
                                        const char* cache_key, char* out_path,
                                        size_t path_size) {
    if (!cache || !cache_key || !out_path || path_size == 0) {
        return FILE_CACHE_ERROR_PARAM;
    }

    build_filepath(cache, cache_key, out_path, path_size);
    return FILE_CACHE_OK;
}
