#include "elpris_api.h"

#include <file_cache.h>
#include <http_client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BASE_URL "https://www.elprisetjustnu.se/api/v1/prices/"

/*10 years*/
#define HISTORICAL_CACHE_TTL (60 * 60 * 24 * 365 * 10)

static FileCacheInstance* g_latest_cache     = NULL;
static FileCacheInstance* g_historical_cache = NULL;

/**
 * @brief Determines if a given UTC time is within Swedish Daylight Saving Time.
 *
 * Sweden observes DST: last Sunday of March → last Sunday of October.
 *
 * @param utc Pointer to a struct tm in UTC.
 * @return 1 if DST, 0 if standard time.
 */
static int is_swedish_dst(const struct tm* utc) {
    int year = utc->tm_year + 1900;

    /* Last Sunday of March */
    struct tm march = {
        .tm_year = year - 1900, .tm_mon = 2, .tm_mday = 31, .tm_hour = 1};
    mktime(&march);
    march.tm_mday -= march.tm_wday;
    time_t dst_start = mktime(&march);

    /* Last Sunday of October */
    struct tm october = {
        .tm_year = year - 1900, .tm_mon = 9, .tm_mday = 31, .tm_hour = 1};
    mktime(&october);
    october.tm_mday -= october.tm_wday;
    time_t dst_end = mktime(&october);

    time_t now = timegm((struct tm*)utc);
    return now >= dst_start && now < dst_end;
}

/**
 * @brief Fills a struct tm with the current Swedish local time (CET/CEST).
 *
 * @param out Pointer to struct tm to populate.
 */
static void swedish_time_from_utc(struct tm* out) {
    time_t now = time(NULL);

    struct tm utc;
    gmtime_r(&now, &utc);

    int offset_hours = is_swedish_dst(&utc) ? 2 : 1;
    now += offset_hours * 3600;

    gmtime_r(&now, out);
}

/**
 * @brief Computes seconds until next Swedish 13:00.
 *
 * Used for latest cache expiration.
 *
 * @return Number of seconds until next 13:00 Swedish time.
 */
static int seconds_until_next_13(void) {
    struct tm se;
    swedish_time_from_utc(&se);

    struct tm target = se;
    target.tm_hour   = 13;
    target.tm_min    = 0;
    target.tm_sec    = 0;

    time_t now    = mktime(&se);
    time_t cutoff = mktime(&target);

    if (cutoff <= now) {
        target.tm_mday += 1;
        cutoff = mktime(&target);
    }

    return (int)difftime(cutoff, now);
}

/**
 * @brief Computes the "latest Elpris date" for a given price group.
 *
 * Rules:
 * - Before 13:00 → today
 * - After 13:00 → tomorrow
 *
 * @param year Pointer to store year
 * @param month Pointer to store month
 * @param day Pointer to store day
 */
static void get_latest_elpris_date(unsigned int* year, unsigned int* month,
                                   unsigned int* day) {
    struct tm se;
    swedish_time_from_utc(&se);

    /* After 13:00 → tomorrow’s prices */
    if (se.tm_hour >= 13) {
        se.tm_mday += 1;
        mktime(&se);
    }

    *year  = se.tm_year + 1900;
    *month = se.tm_mon + 1;
    *day   = se.tm_mday;
}

/**
 * @brief Checks if a given date is the current “latest Elpris date”.
 *
 * @param year Year
 * @param month Month
 * @param day Day
 * @return 1 if latest, 0 otherwise
 */
static int is_latest_date(unsigned int year, unsigned int month,
                          unsigned int day) {
    unsigned int ly, lm, ld;
    get_latest_elpris_date(&ly, &lm, &ld);
    return (year == ly && month == lm && day == ld);
}

/**
 * @brief Returns the appropriate cache instance for a given date.
 *
 * - Latest prices → g_latest_cache (expires at next 13:00)
 * - Historical prices → g_historical_cache (long TTL)
 *
 * @param year Year
 * @param month Month
 * @param day Day
 * @return Pointer to FileCacheInstance
 */
static FileCacheInstance*
get_cache_for_date(unsigned int year, unsigned int month, unsigned int day) {
    if (is_latest_date(year, month, day)) {
        if (!g_latest_cache) {
            FileCacheConfig cfg = {.cache_dir   = ".cache/elpris_latest",
                                   .ttl_seconds = seconds_until_next_13(),
                                   .enabled     = true};
            g_latest_cache      = file_cache_create(&cfg);
        }
        return g_latest_cache;
    }

    if (!g_historical_cache) {
        FileCacheConfig cfg = {.cache_dir   = ".cache/elpris_historical",
                               .ttl_seconds = HISTORICAL_CACHE_TTL,
                               .enabled     = true};
        g_historical_cache  = file_cache_create(&cfg);
    }

    return g_historical_cache;
}

/**
 * @brief Context passed to the HTTP client for async fetch.
 */
typedef struct {
    ElprisApiOnResponse user_callback;
    void*               context;
    FileCacheInstance*  cache;
    char                cache_key[FILE_CACHE_KEY_LENGTH];
} RequestContext;

/**
 * @brief Callback invoked by HTTP client when request completes.
 *
 * Saves response to cache and invokes user callback.
 *
 * @param event Event type (e.g., "RESPONSE", "ERROR")
 * @param response Response body (JSON)
 * @param context RequestContext pointer
 */
static void client_callback(const char* event, const char* response,
                            void* context) {
    RequestContext* ctx = (RequestContext*)context;
    if (!ctx) {
        return;
    }

    if (strcmp(event, "RESPONSE") == 0 && response &&
        (response[0] == '[' || response[0] == '{')) {

        if (ctx->cache) {
            file_cache_save(ctx->cache, ctx->cache_key, response,
                            strlen(response));
        }

        ctx->user_callback((char*)response, ctx->context);
    } else {
        ctx->user_callback(NULL, ctx->context);
    }

    free(ctx);
}

int elpris_api_fetch_async(unsigned int year, unsigned int month,
                           unsigned int day, char price_group[3],
                           ElprisApiOnResponse callback, void* context) {
    if (!callback || !price_group || strlen(price_group) < 2) {
        return -1;
    }

    FileCacheInstance* cache = get_cache_for_date(year, month, day);

    char key_input[128];
    snprintf(key_input, sizeof(key_input), "%04u-%02u-%02u-%s", year, month,
             day, price_group);

    char cache_key[FILE_CACHE_KEY_LENGTH] = {0};

    if (cache &&
        file_cache_generate_key(cache, key_input, cache_key,
                                sizeof(cache_key)) == FILE_CACHE_OK &&
        file_cache_is_valid(cache, cache_key)) {

        char* cached = NULL;
        if (file_cache_load(cache, cache_key, &cached, NULL) == FILE_CACHE_OK) {

            printf("[ELPRIS_API]: Cache hit: %s\n", key_input);
            callback(cached, context);
            free(cached);
            return 0;
        }
    }

    RequestContext* ctx = malloc(sizeof(RequestContext));
    if (!ctx) {
        return -1;
    }

    ctx->user_callback = callback;
    ctx->context       = context;
    ctx->cache         = cache;
    strncpy(ctx->cache_key, cache_key, FILE_CACHE_KEY_LENGTH);

    char url[128];
    snprintf(url, sizeof(url), BASE_URL "%04u/%02u-%02u_%s.json", year, month,
             day, price_group);

    printf("[ELPRIS_API]: Fetching URL: %s\n", url);

    return http_client_get(url, NULL, 30000, client_callback, ctx);
}

int elpris_api_fetch_query_async(const char*         query,
                                 ElprisApiOnResponse callback, void* context) {
    if (!callback || !query || query[0] == '\0' || strcmp(query, "?") == 0) {
        /* Latest endpoint MUST specify price group */
        callback(NULL, context);
        return -1;
    }

    const char* query_start = (query[0] == '?') ? query + 1 : query;

    unsigned int year = 0, month = 0, day = 0;
    char         price_group[4] = {0};
    int          price_found    = 0;

    char* query_copy = strdup(query_start);
    if (!query_copy) {
        callback(NULL, context);
        return -1;
    }

    char* token       = strtok(query_copy, "&");
    int   parse_error = 0;

    while (token && !parse_error) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals         = '\0';
            const char* key = token;
            const char* val = equals + 1;

            if (strcmp(key, "date") == 0) {
                if (sscanf(val, "%4u-%2u-%2u", &year, &month, &day) != 3) {
                    parse_error = 1;
                }
            } else if (strcmp(key, "price") == 0) {
                if (strlen(val) >= 2 && strlen(val) <= 3) {
                    strncpy(price_group, val, 3);
                    price_group[3] = '\0';
                    price_found    = 1;
                } else {
                    parse_error = 1;
                }
            }
        }
        token = strtok(NULL, "&");
    }

    free(query_copy);

    if (parse_error || !price_found) {
        callback(NULL, context);
        return -1;
    }

    if (year == 0) {
        /* No date → latest prices for given price group */
        get_latest_elpris_date(&year, &month, &day);
    }

    return elpris_api_fetch_async(year, month, day, price_group, callback,
                                  context);
}
