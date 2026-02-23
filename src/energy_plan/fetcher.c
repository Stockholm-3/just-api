#include "fetcher.h"

#include "energy_plan/city_registry.h"
#include "file_cache.h"
#include "logger.h"

#include <http_client.h>
#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utils.h>

typedef struct {
    int   done;
    int   success;
    char* response;
} SyncHttpContext;

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
                               uint64_t timeout_ms) {
    SyncHttpContext ctx    = {0};
    HttpClient*     client = NULL;

    if (http_client_init(url, &client, port) != 0) {
        LOG_WARN("FETCHER", "http_client_init failed for: %s", url);
        return NULL;
    }

    client->callback = sync_http_callback;
    client->context  = &ctx;
    client->timeout  = timeout_ms;
    client->state    = HTTP_CLIENT_STATE_INIT;

    while (!ctx.done) {
        uint64_t now = system_monotonic_ms();
        http_client_work(client, now);
    }

    http_client_dispose(&client);
    return ctx.response;
}

static int build_cache_key(const char* raw, char* out, size_t out_size) {
    if (file_cache_normalize_string(raw, out, out_size) != FILE_CACHE_OK) {
        LOG_WARN("FETCHER", "Failed to normalize cache key: %s", raw);
        return -1;
    }
    return 0;
}

static void fetch_city_forecast(FileCacheInstance* cache,
                                const CityEntry* entry, const char* port,
                                uint64_t timeout_ms) {
    char raw_key[sizeof(entry->city) +
                 32]; // city name + two coords with separators
    snprintf(raw_key, sizeof(raw_key), "%s-%.6f-%.6f", entry->city, entry->lat,
             entry->lon);

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (build_cache_key(raw_key, cache_key, sizeof(cache_key)) != 0) {
        return;
    }

    // Always delete the existing entry first so stale data never survives a
    // failed fetch — if the request below fails the key stays absent rather
    // than returning old data to consumers.
    file_cache_invalidate(cache, cache_key);

    char url[512];
    snprintf(url, sizeof(url),
             "http://127.0.0.1:%s/v1/forecast/minutely?lat=%.6f&lon=%.6f", port,
             entry->lat, entry->lon);

    LOG_INFO("FETCHER", "Fetching forecast for %s (%.6f, %.6f)", entry->city,
             entry->lat, entry->lon);

    LOG_INFO("FETCHER", "Fetching from URL: %s", url);

    char* response = run_http_get_sync(url, port, timeout_ms);
    if (!response) {
        LOG_WARN("FETCHER", "HTTP request failed for %s", entry->city);
        return;
    }

    json_error_t err;
    json_t*      root = json_loads(response, 0, &err);
    free(response);

    if (!root) {
        LOG_WARN("FETCHER", "JSON parse error for %s: %s", entry->city,
                 err.text);
        return;
    }

    if (file_cache_save_json(cache, cache_key, root) != FILE_CACHE_OK) {
        LOG_WARN("FETCHER", "Failed to save forecast to cache for %s",
                 entry->city);
        json_decref(root);
        return;
    }

    json_decref(root);

    char path[FILE_CACHE_MAX_PATH_LENGTH];
    if (file_cache_get_filepath(cache, cache_key, path, sizeof(path)) ==
        FILE_CACHE_OK) {
        LOG_INFO("FETCHER", "Saved forecast for %s to: %s", entry->city, path);
    }
}

void fetch_all_city_forecasts(FileCacheInstance* cache, const char* port,
                              uint64_t timeout_ms) {
    if (!cache || !port) {
        LOG_WARN("FETCHER", "Invalid arguments to fetch_all_city_forecasts");
        return;
    }

    CityLoadResult cities = city_registry_load_all(CITY_REGISTRY_FILE);
    if (!cities.entries || cities.count == 0) {
        LOG_WARN("FETCHER", "No cities in registry, skipping weather fetch");
        free(cities.entries);
        return;
    }

    LOG_INFO("FETCHER", "Fetching forecasts for %d cities", cities.count);

    for (int i = 0; i < cities.count; i++) {
        fetch_city_forecast(cache, &cities.entries[i], port, timeout_ms);
    }

    free(cities.entries);
}

int fetch_all_price_groups(FileCacheInstance* cache,
                           const char* cache_key_prefix, const char* port,
                           uint64_t timeout_ms) {
    static const char* groups[] = {"SE1", "SE2", "SE3", "SE4"};
    const size_t       COUNT    = sizeof(groups) / sizeof(groups[0]);

    if (!cache || !cache_key_prefix || !port) {
        LOG_WARN("FETCHER", "Invalid arguments to fetch_all_price_groups");
        return -1;
    }

    json_t* merged = json_array();
    if (!merged) {
        LOG_WARN("FETCHER", "json_array allocation failed");
        return -1;
    }

    for (size_t i = 0; i < COUNT; i++) {
        char url[256];
        snprintf(url, sizeof(url), "http://127.0.0.1:%s/v1/elpris?price=%s",
                 port, groups[i]);

        LOG_INFO("FETCHER", "Fetching elpris for %s", groups[i]);

        char* response = run_http_get_sync(url, port, timeout_ms);
        if (!response) {
            LOG_WARN("FETCHER", "HTTP request failed for %s", groups[i]);
            continue;
        }

        json_error_t err;
        json_t*      root = json_loads(response, 0, &err);
        free(response);

        if (!root) {
            LOG_WARN("FETCHER", "JSON parse error for %s: %s", groups[i],
                     err.text);
            continue;
        }

        if (json_is_array(root)) {
            size_t  j;
            json_t* v;
            json_array_foreach(root, j, v) { json_array_append(merged, v); }
        } else if (json_is_object(root)) {
            json_array_append(merged, root);
        } else {
            LOG_WARN("FETCHER", "Unexpected JSON format for %s", groups[i]);
        }

        json_decref(root);
    }

    char raw_key[FILE_CACHE_KEY_LENGTH];
    snprintf(raw_key, sizeof(raw_key), "%s_merged", cache_key_prefix);

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (build_cache_key(raw_key, cache_key, sizeof(cache_key)) != 0) {
        json_decref(merged);
        return -1;
    }

    // Delete the old merged file before writing so consumers never read a
    // mix of old and new data if the process is interrupted mid-write.
    file_cache_invalidate(cache, cache_key);

    if (file_cache_save_json(cache, cache_key, merged) != FILE_CACHE_OK) {
        LOG_WARN("FETCHER", "Failed to save merged elpris to cache key '%s'",
                 cache_key);
        json_decref(merged);
        return -1;
    }

    json_decref(merged);

    char path[FILE_CACHE_MAX_PATH_LENGTH];
    if (file_cache_get_filepath(cache, cache_key, path, sizeof(path)) ==
        FILE_CACHE_OK) {
        LOG_INFO("FETCHER", "Saved merged elpris to: %s", path);
    }

    return 0;
}
