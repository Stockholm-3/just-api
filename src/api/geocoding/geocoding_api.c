/**
 * geocoding_api.c - Geocoding API implementation
 *
 * Uses the Open-Meteo Geocoding API to search for city coordinates
 * API documentation: https://open-meteo.com/en/docs/geocoding-api
 */

#include <cache_utils/file_cache.h>
#include <ctype.h>
#include <errno.h>
#include <geocoding_api.h>
#include <http_client.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============= Configuration ============= */

#define GEOCODING_API_URL "http://geocoding-api.open-meteo.com/v1/search"
#define DEFAULT_CACHE_DIR "./cache/geo_cache"
#define DEFAULT_CACHE_TTL 604800 /* 7 days */
#define DEFAULT_MAX_RESULTS 10
#define DEFAULT_LANGUAGE "eng"

/* ============= Global State ============= */

static GeocodingConfig g_config = {.cache_dir   = DEFAULT_CACHE_DIR,
                                   .cache_ttl   = DEFAULT_CACHE_TTL,
                                   .use_cache   = true,
                                   .max_results = DEFAULT_MAX_RESULTS,
                                   .language    = DEFAULT_LANGUAGE};

/* Popular cities database pointer (set by weather_location_handler) */
void* g_popular_cities_db = NULL;

/* Cache instance */
static FileCacheInstance* g_geo_cache = NULL;

/* ============= Internal Structures ============= */

typedef struct {
    char*        response_data;
    size_t       response_size;
    int          http_status;
    volatile int completed;
    volatile int error;
} HttpFetchContext;

/* ============= Internal Functions ============= */

static void  http_fetch_callback(const char* event, const char* response);
static int   fetch_url_sync(const char* url, char** response_data,
                            int* http_status);
static int   fetch_from_api(const char* city_name, const char* country,
                            GeocodingResponse** response);
static char* build_api_url(const char* city_name, const char* country,
                           int max_results, const char* language);
static int   parse_geocoding_json(const char*         json_str,
                                  GeocodingResponse** response);

/* ============= Public API Implementation ============= */

int geocoding_api_init(GeocodingConfig* config) {
    if (config) {
        /* Copy user-provided configuration */
        g_config = *config;
    }

    /* Initialize cache */
    FileCacheConfig cache_cfg = {.cache_dir   = g_config.cache_dir,
                                 .ttl_seconds = g_config.cache_ttl,
                                 .enabled     = g_config.use_cache};

    g_geo_cache = file_cache_create(&cache_cfg);
    if (!g_geo_cache) {
        fprintf(stderr, "[GEOCODING] Warning: Failed to initialize cache\n");
    }

    printf("[GEOCODING] API initialized (http_client mode)\n");
    printf("[GEOCODING] Cache dir: %s\n", g_config.cache_dir);
    printf("[GEOCODING] Cache TTL: %d seconds (%d days)\n", g_config.cache_ttl,
           g_config.cache_ttl / 86400);
    printf("[GEOCODING] Cache enabled: %s\n",
           g_config.use_cache ? "yes" : "no");
    printf("[GEOCODING] Language: %s\n", g_config.language);

    return 0;
}

int geocoding_api_search(const char* city_name, const char* country,
                         GeocodingResponse** response) {
    if (!city_name || !response) {
        fprintf(stderr, "[GEOCODING] Invalid parameters\n");
        return -1;
    }

    /* Generate cache key: use only normalized city name (user requested)
     * This makes cache files shared by city regardless of country/language
     * or small input variations (case/whitespace).
     */
    char normalized[256];
    file_cache_normalize_string(city_name, normalized, sizeof(normalized));

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (file_cache_generate_key(g_geo_cache, normalized, cache_key,
                                sizeof(cache_key)) != FILE_CACHE_OK) {
        fprintf(stderr, "[GEOCODING] Failed to generate cache key\n");
        return -2;
    }

    printf("[GEOCODING] Searching for: %s%s%s\n", city_name,
           country ? " in " : "", country ? country : "");

    /* Check cache */
    if (g_config.use_cache && file_cache_is_valid(g_geo_cache, cache_key)) {
        printf("[GEOCODING] Cache HIT - loading from file\n");

        json_t* cached_json = NULL;
        if (file_cache_load_json(g_geo_cache, cache_key,
                                 (void**)&cached_json) == FILE_CACHE_OK) {
            char* json_str = json_dumps(cached_json, 0);
            json_decref(cached_json);

            if (json_str) {
                int result = parse_geocoding_json(json_str, response);
                free(json_str);
                if (result == 0) {
                    return 0; /* Successfully loaded from cache */
                }
            }
        }

        fprintf(stderr, "[GEOCODING] Cache load failed, fetching from API\n");
    } else {
        if (g_config.use_cache) {
            printf("[GEOCODING] Cache MISS - fetching from API\n");
        } else {
            printf("[GEOCODING] Cache disabled - fetching from API\n");
        }
    }

    /* Fetch from API */
    int result = fetch_from_api(city_name, country, response);

    if (result != 0) {
        fprintf(stderr, "[GEOCODING] API fetch failed\n");
        return -3;
    }

    /* Save to cache */
    if (g_config.use_cache && *response) {
        /* Convert response back to JSON for saving */
        json_t* root          = json_object();
        json_t* results_array = json_array();

        for (int i = 0; i < (*response)->count; i++) {
            GeocodingResult* r    = &(*response)->results[i];
            json_t*          item = json_object();

            json_object_set_new(item, "id", json_integer(r->id));
            json_object_set_new(item, "name", json_string(r->name));
            json_object_set_new(item, "latitude", json_real(r->latitude));
            json_object_set_new(item, "longitude", json_real(r->longitude));
            json_object_set_new(item, "country", json_string(r->country));
            json_object_set_new(item, "country_code",
                                json_string(r->country_code));
            if (r->admin1[0]) {
                json_object_set_new(item, "admin1", json_string(r->admin1));
            }
            if (r->admin2[0]) {
                json_object_set_new(item, "admin2", json_string(r->admin2));
            }
            if (r->population > 0) {
                json_object_set_new(item, "population",
                                    json_integer(r->population));
            }
            if (r->timezone[0]) {
                json_object_set_new(item, "timezone", json_string(r->timezone));
            }

            json_array_append_new(results_array, item);
        }

        json_object_set_new(root, "results", results_array);

        if (file_cache_save_json(g_geo_cache, cache_key, root) ==
            FILE_CACHE_OK) {
            printf("[GEOCODING] Saved to cache\n");
        } else {
            fprintf(stderr, "[GEOCODING] Failed to save cache\n");
        }
        json_decref(root);
    }

    return 0;
}

/* Same as geocoding_api_search but do not read or write cache. This is
 * useful for the autocomplete `/v1/cities` endpoint which shouldn't
 * create/update the city cache. */
int geocoding_api_search_no_cache(const char* city_name, const char* country,
                                  GeocodingResponse** response) {
    if (!city_name || !response) {
        return -1;
    }

    /* Directly fetch from API and return parsed results without saving */
    int r = fetch_from_api(city_name, country, response);
    if (r != 0) {
        return r;
    }

    return 0;
}

/* Read-only cache search: try to load from cache, otherwise fetch but do
 * not save to cache. This prevents endpoints like `/v1/cities` from creating
 * new cache files while still benefiting from existing cache entries. */
int geocoding_api_search_readonly_cache(const char*         city_name,
                                        const char*         country,
                                        GeocodingResponse** response) {
    if (!city_name || !response) {
        return -1;
    }

    char normalized[256];
    file_cache_normalize_string(city_name, normalized, sizeof(normalized));

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (file_cache_generate_key(g_geo_cache, normalized, cache_key,
                                sizeof(cache_key)) != FILE_CACHE_OK) {
        return -2;
    }

    /* Try load from cache if valid */
    if (g_config.use_cache && file_cache_is_valid(g_geo_cache, cache_key)) {
        json_t* cached_json = NULL;
        if (file_cache_load_json(g_geo_cache, cache_key,
                                 (void**)&cached_json) == FILE_CACHE_OK) {
            char* json_str = json_dumps(cached_json, 0);
            json_decref(cached_json);

            if (json_str) {
                int result = parse_geocoding_json(json_str, response);
                free(json_str);
                return result;
            }
        }
    }

    /* Cache miss: fetch from API but DO NOT save into cache */
    return fetch_from_api(city_name, country, response);
}

/* ============= Smart Search with 3-Tier Strategy ============= */

/* Forward declarations for popular cities API */
typedef struct {
    char   name[128];
    char   country[64];
    char   country_code[8];
    double latitude;
    double longitude;
    int    population;
} PopularCity;

extern int popular_cities_search(void* db, const char* query,
                                 PopularCity** results, size_t* count,
                                 size_t max_results);

/* Helper: Convert PopularCity array to GeocodingResponse */
static GeocodingResponse* convert_popular_to_geocoding(PopularCity** cities,
                                                       size_t        count) {
    if (!cities || count == 0) {
        return NULL;
    }

    GeocodingResponse* resp = calloc(1, sizeof(GeocodingResponse));
    if (!resp) {
        return NULL;
    }

    resp->results = calloc(count, sizeof(GeocodingResult));
    if (!resp->results) {
        free(resp);
        return NULL;
    }

    resp->count = count;

    for (size_t i = 0; i < count; i++) {
        PopularCity*     pc = cities[i];
        GeocodingResult* gr = &resp->results[i];

        strncpy(gr->name, pc->name, sizeof(gr->name) - 1);
        strncpy(gr->country, pc->country, sizeof(gr->country) - 1);
        strncpy(gr->country_code, pc->country_code,
                sizeof(gr->country_code) - 1);
        gr->latitude    = (float)pc->latitude;
        gr->longitude   = (float)pc->longitude;
        gr->population  = pc->population;
        gr->id          = 0; /* Not available in PopularCity */
        gr->admin1[0]   = '\0';
        gr->admin2[0]   = '\0';
        gr->timezone[0] = '\0';
    }

    return resp;
}

int geocoding_api_search_smart(const char*         query,
                               GeocodingResponse** response) {
    if (!query || !response) {
        fprintf(stderr, "[GEOCODING] Invalid parameters\n");
        return -1;
    }

    /* Validate minimum query length */
    if (strlen(query) < 2) {
        fprintf(stderr, "[GEOCODING] Query too short (min 2 characters)\n");
        return -1;
    }

    /* Tier 1: Search in Popular Cities DB */
    if (g_popular_cities_db) {
        PopularCity* popular_results[10];
        size_t       popular_count = 0;

        int ret = popular_cities_search(g_popular_cities_db, query,
                                        popular_results, &popular_count, 10);

        if (ret == 0 && popular_count > 0) {
            printf("[GEOCODING] Found %zu results in popular cities DB\n",
                   popular_count);

            *response =
                convert_popular_to_geocoding(popular_results, popular_count);

            if (*response) {
                return 0; /* SUCCESS - found in local DB */
            }
        }
    }

    /* Tier 2: Search in exact cache match */
    if (geocoding_api_search_readonly_cache(query, NULL, response) == 0) {
        if (response && *response && (*response)->count > 0) {
            printf("[GEOCODING] Found %d results in cache\n",
                   (*response)->count);
            return 0; /* SUCCESS - found in cache */
        }
    }

    /* Tier 3: Fallback to API */
    printf("[GEOCODING] Cache miss, fetching from API for query: %s\n", query);

    int api_result = fetch_from_api(query, NULL, response);

    if (api_result == 0 && *response) {
        /* Save to cache for future requests */
        /* Note: fetch_from_api already handles caching internally */
        printf("[GEOCODING] API returned %d results\n", (*response)->count);
    }

    return api_result;
}

int geocoding_api_search_detailed(const char* city_name, const char* region,
                                  const char*         country,
                                  GeocodingResponse** response) {
    /* First, perform a normal search */
    int result = geocoding_api_search(city_name, country, response);

    if (result != 0 || !response || !*response) {
        return result;
    }

    /* If a region is specified, filter the results */
    if (region && region[0] != '\0') {
        /* Normalize region token: convert underscores/+ to spaces so
         * inputs like "South_Dakota" or "South+Dakota" match "South Dakota".
         */
        char region_norm[128];
        strncpy(region_norm, region, sizeof(region_norm) - 1);
        region_norm[sizeof(region_norm) - 1] = '\0';
        for (size_t k = 0; region_norm[k]; ++k) {
            if (region_norm[k] == '_' || region_norm[k] == '+') {
                region_norm[k] = ' ';
            }
        }

        GeocodingResponse* filtered = malloc(sizeof(GeocodingResponse));
        if (!filtered) {
            return -1;
        }

        filtered->results =
            malloc(sizeof(GeocodingResult) * (*response)->count);
        if (!filtered->results) {
            free(filtered);
            return -1;
        }

        filtered->count = 0;

        /* Filter by region */
        for (int i = 0; i < (*response)->count; i++) {
            GeocodingResult* r = &(*response)->results[i];
            if ((r->admin1[0] && strcasestr(r->admin1, region_norm) != NULL) ||
                (r->admin2[0] && strcasestr(r->admin2, region_norm) != NULL)) {
                filtered->results[filtered->count] = *r;
                filtered->count++;
            }
        }

        /* If filtered results are found, replace the original response */
        if (filtered->count > 0) {
            /* Reallocate memory to the exact count */
            GeocodingResult* temp = realloc(
                filtered->results, sizeof(GeocodingResult) * filtered->count);
            if (temp) {
                filtered->results = temp;
            }

            geocoding_api_free_response(*response);
            *response = filtered;
        } else {
            /* If nothing is found after filtering, keep the original results */
            free(filtered->results);
            free(filtered);
            printf("[GEOCODING] No results match region '%s', returning all "
                   "results\n",
                   region);
        }
    }

    return 0;
}

GeocodingResult* geocoding_api_get_best_result(GeocodingResponse* response,
                                               const char*        country) {
    if (!response || response->count == 0) {
        return NULL;
    }

    /* If a country is provided, prefer results that match it. Match by
     * country code first (case-insensitive), then by country name. If
     * multiple matches exist, pick the one with largest population. If no
     * match is found, fall back to the result with the largest population or
     * the first result.
     */
    GeocodingResult* best = NULL;

    if (country && country[0] != '\0') {
        /* try country code match (case-insensitive) */
        for (int i = 0; i < response->count; ++i) {
            GeocodingResult* r = &response->results[i];
            if (r->country_code[0] != '\0') {
                if (strcasecmp(r->country_code, country) == 0) {
                    if (!best || r->population > best->population) {
                        best = r;
                    }
                }
            }
        }

        /* if none by code, try matching by country name (case-insensitive
         * substr) */
        if (!best) {
            for (int i = 0; i < response->count; ++i) {
                GeocodingResult* r = &response->results[i];
                if (r->country[0] != '\0') {
                    if (strcasecmp(r->country, country) == 0 ||
                        strcasestr(r->country, country) != NULL) {
                        if (!best || r->population > best->population) {
                            best = r;
                        }
                    }
                }
            }
        }
    }

    /* If still no best, pick the highest-population result, or first as
     * fallback */
    if (!best) {
        for (int i = 0; i < response->count; ++i) {
            GeocodingResult* r = &response->results[i];
            if (!best || r->population > best->population) {
                best = r;
            }
        }
    }

    if (!best) {
        return &response->results[0];
    }
    return best;
}

void geocoding_api_free_response(GeocodingResponse* response) {
    if (response) {
        if (response->results) {
            free(response->results);
        }
        free(response);
    }
}

int geocoding_api_clear_cache(void) {
    if (file_cache_clear(g_geo_cache) == FILE_CACHE_OK) {
        printf("[GEOCODING] Cache cleared\n");
        return 0;
    }

    fprintf(stderr, "[GEOCODING] Failed to clear cache\n");
    return -1;
}

void geocoding_api_cleanup(void) {
    if (g_geo_cache) {
        file_cache_destroy(g_geo_cache);
        g_geo_cache = NULL;
    }
    printf("[GEOCODING] API cleaned up\n");
}

int geocoding_api_format_result(GeocodingResult* result, char* buffer,
                                size_t buffer_size) {
    if (!result || !buffer || buffer_size == 0) {
        return -1;
    }

    /* Format: "Name, Region, Country (lat, lon)" */
    int written = 0;

    written +=
        snprintf(buffer + written, buffer_size - written, "%s", result->name);

    if (result->admin1[0]) {
        written += snprintf(buffer + written, buffer_size - written, ", %s",
                            result->admin1);
    }

    written += snprintf(buffer + written, buffer_size - written, ", %s",
                        result->country);

    written += snprintf(buffer + written, buffer_size - written,
                        " (%.4f, %.4f)", result->latitude, result->longitude);

    return 0;
}

/* ============= Internal Functions Implementation ============= */

/* ============= HTTP Client Integration ============= */

static HttpFetchContext* g_fetch_context = NULL;

static void http_fetch_callback(const char* event, const char* response) {
    if (!g_fetch_context) {
        return;
    }

    if (strcmp(event, "RESPONSE") == 0) {
        g_fetch_context->response_data = strdup(response);
        g_fetch_context->response_size = strlen(response);
        g_fetch_context->http_status   = 200;
        g_fetch_context->completed     = 1;
    } else if (strcmp(event, "ERROR") == 0 || strcmp(event, "TIMEOUT") == 0) {
        g_fetch_context->error     = 1;
        g_fetch_context->completed = 1;
    }
}

static int fetch_url_sync(const char* url, char** response_data,
                          int* http_status) {
    HttpFetchContext context = {0};
    g_fetch_context          = &context;

    http_client_get(url, 30000, http_fetch_callback, NULL);

    /* Poll event loop - fast iterations, no sleep! */
    time_t start_time      = time(NULL);
    time_t timeout_seconds = 30;

    while (!context.completed) {
        smw_work(0); /* Pass 0 if monotonic time unavailable */

        /* Check timeout (1 second granularity) */
        if (time(NULL) - start_time > timeout_seconds) {
            fprintf(stderr, "[GEOCODING] Timeout waiting for response\n");
            break;
        }
    }

    g_fetch_context = NULL;

    if (context.error || !context.completed) {
        if (context.response_data) {
            free(context.response_data);
        }
        return -1;
    }

    *response_data = context.response_data;
    *http_status   = context.http_status;

    return 0;
}

/* Helper function: simple URL encoding */
static int url_encode_char(const char* src, int src_len, char* dst,
                           int dst_size) {
    int dst_pos = 0;
    for (int i = 0; i < src_len && dst_pos + 3 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            dst[dst_pos++] = c;
        } else if (c == ' ') {
            dst[dst_pos++] = '+';
        } else {
            if (dst_pos + 2 >= dst_size) {
                break;
            }
            dst_pos += snprintf(dst + dst_pos, dst_size - dst_pos, "%%%02X", c);
        }
    }
    dst[dst_pos] = '\0';
    return dst_pos;
}

/**
 * Build URL for API request
 */
static char* build_api_url(const char* city_name, const char* country,
                           int max_results, const char* language) {
    char* url = malloc(2048);
    if (!url) {
        return NULL;
    }

    /* Encode city name */
    char encoded_city[512];
    url_encode_char(city_name, strlen(city_name), encoded_city,
                    sizeof(encoded_city));

    int written =
        snprintf(url, 2048, "%s?name=%s&count=%d&language=%s&format=json",
                 GEOCODING_API_URL, encoded_city, max_results, language);

    if (country) {
        /* Encode country if present */
        char encoded_country[256];
        url_encode_char(country, strlen(country), encoded_country,
                        sizeof(encoded_country));
        written += snprintf(url + written, 2048 - written, "&country=%s",
                            encoded_country);
    }

    return url;
}

/**
 * Parse JSON response from the API
 */
static int parse_geocoding_json(const char*         json_str,
                                GeocodingResponse** response) {
    json_error_t error;
    json_t*      root = json_loadb(json_str, strlen(json_str), 0, &error);

    if (!root) {
        fprintf(stderr, "[GEOCODING] JSON parse error: %s\n", error.text);
        return -1;
    }

    json_t* results_array = json_object_get(root, "results");
    if (!results_array) {
        /* No results */
        *response            = calloc(1, sizeof(GeocodingResponse));
        (*response)->count   = 0;
        (*response)->results = NULL;
        json_decref(root);
        return 0;
    }

    if (!json_is_array(results_array)) {
        fprintf(stderr, "[GEOCODING] Invalid results format\n");
        json_decref(root);
        return -2;
    }

    size_t count = json_array_size(results_array);
    if (count == 0) {
        *response            = calloc(1, sizeof(GeocodingResponse));
        (*response)->count   = 0;
        (*response)->results = NULL;
        json_decref(root);
        return 0;
    }

    /* Allocate memory */
    *response = calloc(1, sizeof(GeocodingResponse));
    if (!*response) {
        json_decref(root);
        return -3;
    }

    (*response)->results = calloc(count, sizeof(GeocodingResult));
    if (!(*response)->results) {
        free(*response);
        json_decref(root);
        return -4;
    }

    (*response)->count = count;

    /* Parse each result */
    for (size_t i = 0; i < count; i++) {
        json_t*          item   = json_array_get(results_array, i);
        GeocodingResult* result = &(*response)->results[i];

        /* Required fields */
        json_t* id           = json_object_get(item, "id");
        json_t* name         = json_object_get(item, "name");
        json_t* lat          = json_object_get(item, "latitude");
        json_t* lon          = json_object_get(item, "longitude");
        json_t* country_name = json_object_get(item, "country");
        json_t* country_code = json_object_get(item, "country_code");

        if (id) {
            result->id = json_integer_value(id);
        }
        if (name) {
            strncpy(result->name, json_string_value(name),
                    sizeof(result->name) - 1);
        }
        if (lat) {
            result->latitude = json_real_value(lat);
        }
        if (lon) {
            result->longitude = json_real_value(lon);
        }
        if (country_name) {
            strncpy(result->country, json_string_value(country_name),
                    sizeof(result->country) - 1);
        }
        if (country_code) {
            strncpy(result->country_code, json_string_value(country_code),
                    sizeof(result->country_code) - 1);
        }

        /* Optional fields */
        json_t* admin1     = json_object_get(item, "admin1");
        json_t* admin2     = json_object_get(item, "admin2");
        json_t* population = json_object_get(item, "population");
        json_t* timezone   = json_object_get(item, "timezone");

        if (admin1) {
            strncpy(result->admin1, json_string_value(admin1),
                    sizeof(result->admin1) - 1);
        }
        if (admin2) {
            strncpy(result->admin2, json_string_value(admin2),
                    sizeof(result->admin2) - 1);
        }
        if (population) {
            result->population = json_integer_value(population);
        }
        if (timezone) {
            strncpy(result->timezone, json_string_value(timezone),
                    sizeof(result->timezone) - 1);
        }
    }

    json_decref(root);
    return 0;
}

/**
 * Fetch data from API
 */
static int fetch_from_api(const char* city_name, const char* country,
                          GeocodingResponse** response) {
    /* Build URL */
    char* url = build_api_url(city_name, country, g_config.max_results,
                              g_config.language);
    if (!url) {
        return -1;
    }

    printf("[GEOCODING] Fetching: %s\n", url);

    char* response_data = NULL;
    int   http_status   = 0;

    int result = fetch_url_sync(url, &response_data, &http_status);
    free(url);

    if (result != 0 || !response_data) {
        return -2;
    }

    /* Parse JSON */
    int parse_result = parse_geocoding_json(response_data, response);

    free(response_data);

    if (parse_result != 0) {
        return -3;
    }

    printf("[GEOCODING] Found %d result(s)\n", (*response)->count);
    return 0;
}
