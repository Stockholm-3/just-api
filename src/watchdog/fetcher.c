#include "fetcher.h"

#include "file_cache.h"

#include <http_client.h>
#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utils.h>

/* =========================
   Internal Structures
   ========================= */

typedef struct {
    int   done;
    int   success;
    char* response;
} SyncHttpContext;

/* =========================
   CityPrice Helpers
   ========================= */

void free_city_price_list(CityPrice* list, size_t count) {
    if (!list)
        return;
    for (size_t i = 0; i < count; i++) {
        free(list[i].city);
        free(list[i].price);
    }
    free(list);
}

CityPrice* load_city_price_file(const char* filename, size_t* count) {
    json_error_t error;
    json_t*      root = json_load_file(filename, 0, &error);
    if (!root) {
        fprintf(stderr, "JSON error in %s (line %d): %s\n", filename,
                error.line, error.text);
        return NULL;
    }

    if (!json_is_array(root)) {
        fprintf(stderr, "Root element is not an array\n");
        json_decref(root);
        return NULL;
    }

    size_t     n    = json_array_size(root);
    CityPrice* list = calloc(n, sizeof(CityPrice));
    if (!list) {
        perror("calloc");
        json_decref(root);
        return NULL;
    }

    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        json_t* entry = json_array_get(root, i);
        if (!json_is_object(entry))
            continue;

        json_t* city  = json_object_get(entry, "City");
        json_t* price = json_object_get(entry, "Price");
        if (!json_is_string(city) || !json_is_string(price))
            continue;

        list[out].city  = strdup(json_string_value(city));
        list[out].price = strdup(json_string_value(price));
        if (!list[out].city || !list[out].price) {
            perror("strdup");
            free_city_price_list(list, out);
            json_decref(root);
            return NULL;
        }
        out++;
    }

    json_decref(root);
    *count = out;
    return list;
}

/* =========================
   HTTP Fetch Helpers
   ========================= */

static void sync_http_callback(const char* event, const char* response,
                               void* context) {
    SyncHttpContext* ctx = context;
    ctx->done            = 1;
    if (ctx->response) {
        free(ctx->response);
        ctx->response = NULL;
    }
    if (strcmp(event, "RESPONSE") == 0 && response) {
        ctx->success  = 1;
        ctx->response = strdup(response);
        if (!ctx->response) {
            perror("strdup");
            ctx->success = 0;
        }
    } else {
        ctx->success = 0;
    }
}

static char* run_http_get_sync(const char* url, const char* port,
                               uint64_t timeout) {
    SyncHttpContext ctx    = {0};
    HttpClient*     client = NULL;
    if (http_client_init(url, &client, port) != 0) {
        fprintf(stderr, "http_client_init failed\n");
        return NULL;
    }

    client->callback = sync_http_callback;
    client->context  = &ctx;
    client->timeout  = timeout;
    client->state    = HTTP_CLIENT_STATE_INIT;

    while (!ctx.done) {
        uint64_t now = system_monotonic_ms();
        http_client_work(client, now);
        usleep(1000);
    }

    http_client_dispose(&client);
    return ctx.response;
}

int fetch_all_price_groups_sync(FileCacheInstance* cache,
                                const char* cache_key_prefix, const char* port,
                                uint64_t timeout) {
    static const char* groups[] = {"SE1", "SE2", "SE3", "SE4"};
    const size_t       COUNT    = sizeof(groups) / sizeof(groups[0]);

    if (!cache || !cache_key_prefix) {
        fprintf(stderr, "Invalid cache instance or key prefix\n");
        return -1;
    }

    json_t* merged = json_array();
    if (!merged) {
        fprintf(stderr, "json_array failed\n");
        return -1;
    }

    for (size_t i = 0; i < COUNT; i++) {
        char url[256];
        snprintf(url, sizeof(url), "http://127.0.0.1:%s/v1/elpris?price=%s",
                 port, groups[i]);
        printf("Fetching %s...\n", groups[i]);

        char* response = run_http_get_sync(url, port, timeout);
        if (!response) {
            fprintf(stderr, "Failed to fetch %s\n", groups[i]);
            continue;
        }

        json_error_t err;
        json_t*      root = json_loads(response, 0, &err);
        free(response);
        if (!root) {
            fprintf(stderr, "JSON parse error: %s\n", err.text);
            continue;
        }

        if (json_is_array(root)) {
            size_t  j;
            json_t* v;
            json_array_foreach(root, j, v) { json_array_append(merged, v); }
        } else if (json_is_object(root)) {
            json_array_append(merged, root);
        } else {
            fprintf(stderr, "Unexpected JSON format\n");
        }

        json_decref(root);
    }

    /* Normalize cache key */
    char cache_key_input[FILE_CACHE_KEY_LENGTH];
    snprintf(cache_key_input, sizeof(cache_key_input), "%s_merged",
             cache_key_prefix);

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (file_cache_normalize_string(cache_key_input, cache_key,
                                    sizeof(cache_key)) != FILE_CACHE_OK) {
        fprintf(stderr, "Failed to normalize cache key\n");
        json_decref(merged);
        return -1;
    }

    /* -----------------------------
       Save merged JSON into cache
       ----------------------------- */
    if (file_cache_save_json(cache, cache_key, merged) != FILE_CACHE_OK) {
        fprintf(stderr, "Failed to save merged JSON to cache key '%s'\n",
                cache_key);
        json_decref(merged);
        return -1;
    }

    /* Optional: acquire lock only if needed for later concurrent access */
    FileCacheLock* lock = NULL;
    if (file_cache_lock(cache, cache_key, FILE_CACHE_LOCK_EXCLUSIVE, &lock) ==
        FILE_CACHE_OK) {
        /* Do something with locked cache if needed */
        file_cache_unlock(lock);
    }

    /* Print file path for debugging */
    char path[FILE_CACHE_MAX_PATH_LENGTH];
    if (file_cache_get_filepath(cache, cache_key, path, sizeof(path)) ==
        FILE_CACHE_OK) {
        printf("Saved merged data to cache file: %s\n", path);
    } else {
        printf("Merged data saved to cache (path unknown)\n");
    }

    json_decref(merged);
    return 0;
}
