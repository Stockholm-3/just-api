#include "elpris_api.h"

#include <file_cache.h>
#include <http_client.h>
#include <http_utils.h>
#include <logger/logger.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BASE_URL "https://www.elprisetjustnu.se/api/v1/prices/"
#define LATEST_CACHE_DIR "./cache/elpris_latest"
#define HISTORICAL_CACHE_DIR "./cache/elpris_historical"
#define HISTORICAL_CACHE_TTL (60 * 60 * 24 * 365 * 10) /* 10 years */

/* Historical cache is a long-lived singleton — its TTL policy never changes. */
static FileCacheInstance* g_historical_cache = NULL;

/*
 * The "latest" cache is date-stamped. If the date we consider "latest"
 * advances (i.e. it's past 13:00 and tomorrow's prices are now current),
 * we tear down the old instance and rebuild it with a fresh TTL so we
 * never serve stale data thinking it's current.
 */
static FileCacheInstance* g_latest_cache       = NULL;
static unsigned int       g_latest_cache_year  = 0;
static unsigned int       g_latest_cache_month = 0;
static unsigned int       g_latest_cache_day   = 0;

/* ============================================================================
 * Swedish Time Utilities
 * ========================================================================= */

static int is_swedish_dst(const struct tm* utc) {
    int year = utc->tm_year + 1900;

    /* Last Sunday of March at 01:00 UTC */
    struct tm march = {
        .tm_year = year - 1900, .tm_mon = 2, .tm_mday = 31, .tm_hour = 1};
    mktime(&march);
    march.tm_mday -= march.tm_wday;
    time_t dst_start = mktime(&march);

    /* Last Sunday of October at 01:00 UTC */
    struct tm october = {
        .tm_year = year - 1900, .tm_mon = 9, .tm_mday = 31, .tm_hour = 1};
    mktime(&october);
    october.tm_mday -= october.tm_wday;
    time_t dst_end = mktime(&october);

    time_t now = timegm((struct tm*)utc);
    return now >= dst_start && now < dst_end;
}

static void swedish_time_now(struct tm* out) {
    time_t    now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);

    int offset_hours = is_swedish_dst(&utc) ? 2 : 1;
    now += (time_t)(offset_hours * 3600);

    gmtime_r(&now, out);
}

/*
 * Returns seconds until the next 13:00 Swedish time.
 * This is used as the TTL for the "latest" cache entry so it expires
 * exactly when new prices become available.
 */
static int seconds_until_next_13(void) {
    struct tm se;
    swedish_time_now(&se);

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

/*
 * Returns the date whose prices are considered "latest":
 *   - Before 13:00 Swedish time → today
 *   - At or after 13:00           → tomorrow (next-day prices are published)
 */
static void get_latest_date(unsigned int* year, unsigned int* month,
                            unsigned int* day) {
    struct tm se;
    swedish_time_now(&se);

    if (se.tm_hour >= 13) {
        se.tm_mday += 1;
        mktime(&se); /* normalise overflow */
    }

    *year  = (unsigned int)(se.tm_year + 1900);
    *month = (unsigned int)(se.tm_mon + 1);
    *day   = (unsigned int)se.tm_mday;
}

/* ============================================================================
 * Cache Management
 * ========================================================================= */

/*
 * Returns the appropriate cache instance for the requested date.
 *
 * Key fix: the "latest" cache singleton is invalidated and recreated whenever
 * the date we consider "latest" has advanced. This prevents the old instance
 * (with a near-zero or already-expired TTL) from being reused for the new
 * current date and incorrectly serving — or immediately discarding — fresh
 * data.
 *
 * Separate cache directories ensure that historical (10-year TTL) and latest
 * (TTL-until-13:00) policies never interfere with each other.
 */
static FileCacheInstance* get_cache(unsigned int year, unsigned int month,
                                    unsigned int day) {
    unsigned int latest_year, latest_month, latest_day;
    get_latest_date(&latest_year, &latest_month, &latest_day);

    int is_latest =
        (year == latest_year && month == latest_month && day == latest_day);

    if (is_latest) {
        /* Invalidate if the "latest" date has rolled over since we last built
         * the cache instance. */
        int date_changed =
            (g_latest_cache != NULL) && (g_latest_cache_year != latest_year ||
                                         g_latest_cache_month != latest_month ||
                                         g_latest_cache_day != latest_day);

        if (date_changed) {
            LOG_DEBUG("ELPRIS",
                      "Latest date rolled over from %04u-%02u-%02u to "
                      "%04u-%02u-%02u — invalidating latest cache",
                      g_latest_cache_year, g_latest_cache_month,
                      g_latest_cache_day, latest_year, latest_month,
                      latest_day);
            file_cache_destroy(g_latest_cache);
            g_latest_cache       = NULL;
            g_latest_cache_year  = 0;
            g_latest_cache_month = 0;
            g_latest_cache_day   = 0;
        }

        if (!g_latest_cache) {
            int ttl = seconds_until_next_13();
            LOG_DEBUG("ELPRIS",
                      "Creating latest cache for %04u-%02u-%02u (TTL=%ds)",
                      latest_year, latest_month, latest_day, ttl);

            FileCacheConfig cfg = {.cache_dir   = LATEST_CACHE_DIR,
                                   .ttl_seconds = ttl,
                                   .enabled     = true};
            g_latest_cache      = file_cache_create(&cfg);

            g_latest_cache_year  = latest_year;
            g_latest_cache_month = latest_month;
            g_latest_cache_day   = latest_day;
        }

        return g_latest_cache;
    }

    /* Historical — one long-lived singleton is fine; TTL never changes. */
    if (!g_historical_cache) {
        FileCacheConfig cfg = {.cache_dir   = HISTORICAL_CACHE_DIR,
                               .ttl_seconds = HISTORICAL_CACHE_TTL,
                               .enabled     = true};
        g_historical_cache  = file_cache_create(&cfg);
    }

    return g_historical_cache;
}

/* ============================================================================
 * Query Parsing
 * ========================================================================= */

typedef struct {
    unsigned int year;
    unsigned int month;
    unsigned int day;
    char         price_group[4];
    int          valid;
} ParsedQuery;

static ParsedQuery parse_query(const char* query) {
    ParsedQuery result = {0};

    if (!query || query[0] == '\0' || strcmp(query, "?") == 0) {
        return result;
    }

    const char* start = (query[0] == '?') ? query + 1 : query;
    char*       copy  = strdup(start);
    if (!copy) {
        return result;
    }

    int   has_price = 0;
    char* token     = strtok(copy, "&");

    while (token) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals         = '\0';
            const char* key = token;
            const char* val = equals + 1;

            if (strcmp(key, "date") == 0) {
                if (sscanf(val, "%4u-%2u-%2u", &result.year, &result.month,
                           &result.day) != 3) {
                    free(copy);
                    return result;
                }
            } else if (strcmp(key, "price") == 0) {
                if (strlen(val) >= 2 && strlen(val) <= 3) {
                    strncpy(result.price_group, val, 3);
                    result.price_group[3] = '\0';
                    has_price             = 1;
                } else {
                    free(copy);
                    return result;
                }
            }
        }
        token = strtok(NULL, "&");
    }

    free(copy);

    if (!has_price) {
        return result;
    }

    /* No date supplied — use latest */
    if (result.year == 0) {
        get_latest_date(&result.year, &result.month, &result.day);
    }

    result.valid = 1;
    return result;
}

/* ============================================================================
 * Async HTTP Handling
 * ========================================================================= */

typedef struct {
    HTTPServerConnection* conn;
    FileCacheInstance*    cache;
    char                  cache_key[FILE_CACHE_KEY_LENGTH];
} FetchContext;

static void on_http_response(const char* event, const char* response,
                             void* context) {
    FetchContext* ctx = (FetchContext*)context;
    if (!ctx || !ctx->conn) {
        return;
    }

    if (strcmp(event, "RESPONSE") == 0 && response &&
        (response[0] == '[' || response[0] == '{')) {

        /* Cache the response */
        if (ctx->cache) {
            file_cache_save(ctx->cache, ctx->cache_key, response,
                            strlen(response));
        }

        send_response(ctx->conn, 200, "application/json", response,
                      strlen(response));
    } else {
        if (strcmp(event, "ERROR") == 0) {
            LOG_ERROR("ELPRIS", "HTTP error: %s",
                      response ? response : "unknown");
        } else if (strcmp(event, "TIMEOUT") == 0) {
            LOG_ERROR("ELPRIS", "Request timeout");
        } else {
            LOG_WARN("ELPRIS", "Invalid response");
        }

        send_json_message(
            ctx->conn, 503,
            "Unable to fetch electricity prices. "
            "The date may be unavailable or the service is down.");
    }

    free(ctx);
}

/* ============================================================================
 * Public API
 * ========================================================================= */

int elpris_api_fetch_and_respond(HTTPServerConnection* conn,
                                 const char*           query) {
    if (!conn) {
        return -1;
    }

    ParsedQuery parsed = parse_query(query);
    if (!parsed.valid) {
        send_json_message(
            conn, 400,
            "Invalid query. Format: ?price=SE3&date=2024-01-15. "
            "Note: date is optional, omitting it returns the latest price.");
        return -1;
    }

    FileCacheInstance* cache = get_cache(parsed.year, parsed.month, parsed.day);

    /* Build cache key */
    char key_input[128];
    snprintf(key_input, sizeof(key_input), "%04u-%02u-%02u-%s", parsed.year,
             parsed.month, parsed.day, parsed.price_group);

    char cache_key[FILE_CACHE_KEY_LENGTH] = {0};

    /* Check cache */
    if (cache &&
        file_cache_generate_key(cache, key_input, cache_key,
                                sizeof(cache_key)) == FILE_CACHE_OK &&
        file_cache_is_valid(cache, cache_key)) {

        char* cached = NULL;
        if (file_cache_load(cache, cache_key, &cached, NULL) == FILE_CACHE_OK) {
            LOG_DEBUG("ELPRIS", "Cache hit: %s", key_input);
            send_response(conn, 200, "application/json", cached,
                          strlen(cached));
            free(cached);
            return 0;
        }
    }

    /* Cache miss — fetch asynchronously */
    FetchContext* ctx = malloc(sizeof(FetchContext));
    if (!ctx) {
        send_json_message(conn, 500, "Memory allocation failed");
        return -1;
    }

    ctx->conn  = conn;
    ctx->cache = cache;
    strncpy(ctx->cache_key, cache_key, FILE_CACHE_KEY_LENGTH);

    char url[128];
    snprintf(url, sizeof(url), BASE_URL "%04u/%02u-%02u_%s.json", parsed.year,
             parsed.month, parsed.day, parsed.price_group);

    LOG_DEBUG("ELPRIS", "Fetching: %s", url);

    return http_client_get(url, NULL, 30000, on_http_response, ctx);
}
