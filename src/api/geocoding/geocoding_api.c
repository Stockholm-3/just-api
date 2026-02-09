/**
 * geocoding_api.c - Implementation av geokodnings-API
 *
 * Använder Open-Meteo Geocoding API för att söka efter koordinater
 * för städer.
 * API-dokumentation: https://open-meteo.com/en/docs/geocoding-api
 */

#include <cache_utils/file_cache.h>
#include <ctype.h>
#include <errno.h>
#include <geocoding_api.h>
#include <geocoding_http.h>
#include <geocoding_parser.h>
#include <http_client.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============= Konfiguration ============= */

#define GEOCODING_API_URL "http://geocoding-api.open-meteo.com/v1/search"
#define DEFAULT_CACHE_DIR "./cache/geo_cache"
#define DEFAULT_CACHE_TTL 604800 /* 7 days */
#define DEFAULT_MAX_RESULTS 10
#define DEFAULT_LANGUAGE "eng"

/* ============= Globalt tillstånd ============= */

static GeocodingConfig g_config = {.cache_dir   = DEFAULT_CACHE_DIR,
                                   .cache_ttl   = DEFAULT_CACHE_TTL,
                                   .use_cache   = true,
                                   .max_results = DEFAULT_MAX_RESULTS,
                                   .language    = DEFAULT_LANGUAGE};

/* Pekare till databas med populära städer (sätts av weather_location_handler)
 */
void* g_popular_cities_db = NULL;

/* Cache instance */
static FileCacheInstance* g_geo_cache = NULL;

/* ============= Interna strukturer ============= */

/* ============= Interna funktioner ============= */

static int fetch_from_api(const char* city_name, const char* country,
                          GeocodingResponse** response);
/* JSON parsing moved to geocoding_parser.c */

/* ============= Implementation av publikt API ============= */

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

    /* Generera cache-nyckel: använd endast normaliserat stadsnamn
     * Detta gör cache-filer delade per stad oberoende av land/språk
     * eller små indatavariationer (versaler/whitespace).
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

    /* Kontrollera cache */
    if (g_config.use_cache && file_cache_is_valid(g_geo_cache, cache_key)) {
        printf("[GEOCODING] Cache HIT - loading from file\n");

        json_t* cached_json = NULL;
        if (file_cache_load_json(g_geo_cache, cache_key,
                                 (void**)&cached_json) == FILE_CACHE_OK) {
            char* json_str = json_dumps(cached_json, 0);
            json_decref(cached_json);

            if (json_str) {
                int result = geocoding_parse_json(json_str, response);
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

    /* Hämta från API */
    int result = fetch_from_api(city_name, country, response);

    if (result != 0) {
        fprintf(stderr, "[GEOCODING] API fetch failed\n");
        return -3;
    }

    /* Spara till cache */
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

            /* FIX: Flyttad innanfor loopen — item deklareras i loop-scopet
             * och varje resultat maste laggas till i arrayen per iteration. */
            json_array_append_new(results_array, item);
        }

        /* FIX: Flyttade json_object_set_new, file_cache_save_json och
         * json_decref innanfor if-blocket — root och results_array
         * deklareras i detta scope och far inte anvandas utanfor. */
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
                int result = geocoding_parse_json(json_str, response);
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
} PopularCity;

int popular_cities_search(void* db, const char* query,
                          PopularCity** results, size_t* count,
                          size_t max_results);

/* Helper: Convert PopularCity array to GeocodingResponse */
static GeocodingResponse*
convert_popular_to_geocoding(PopularCity** cities, size_t count) {
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
        /* Normalize region token: convert underscores/+ to spaces */
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
            if ((r->admin1[0] &&
                 strcasestr(r->admin1, region_norm) != NULL) ||
                (r->admin2[0] &&
                 strcasestr(r->admin2, region_norm) != NULL)) {
                filtered->results[filtered->count] = *r;
                filtered->count++;
            }
        }

        /* If filtered results are found, replace the original response */
        if (filtered->count > 0) {
            GeocodingResult* temp =
                realloc(filtered->results,
                        sizeof(GeocodingResult) * filtered->count);
            if (temp) {
                filtered->results = temp;
            }

            geocoding_api_free_response(*response);
            *response = filtered;
        } else {
            free(filtered->results);
            free(filtered);
            printf("[GEOCODING] No results match region '%s', returning all "
                   "results\n",
                   region);
        }
    }

    return 0;
}

/* FIX: geocoding_api_get_best_result — best-variabeln saknades,
 * kommentar var oavslutad, och matchade resultat returnerades aldrig. */
GeocodingResult* geocoding_api_get_best_result(GeocodingResponse* response,
                                               const char*        country) {
    if (!response || response->count == 0) {
        return NULL;
    }

    GeocodingResult* best = NULL;

    /* If a country is provided, prefer results that match it. Match by
     * country code first (case-insensitive), then by country name. */
    if (country && country[0] != '\0') {
        /* try country code match (case-insensitive) */
        for (int i = 0; i < response->count; ++i) {
            GeocodingResult* r = &response->results[i];
            if (r->country_code[0] != '\0' &&
                strcasecmp(r->country_code, country) == 0) {
                best = r;
                break;
            }
        }

        /* if none by code, try matching by country name
         * (case-insensitive substr) */
        if (!best) {
            for (int i = 0; i < response->count; ++i) {
                GeocodingResult* r = &response->results[i];
                if (r->country[0] != '\0' &&
                    (strcasecmp(r->country, country) == 0 ||
                     strcasestr(r->country, country) != NULL)) {
                    best = r;
                    break;
                }
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

    written += snprintf(buffer + written, buffer_size - written, "%s",
                        result->name);

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

/* HTTP and URL building implemented in geocoding_http.c */

/* JSON parsing implemented in src/api/geocoding/geocoding_parser.c */

/** Fetch data from API */
static int fetch_from_api(const char* city_name, const char* country,
                          GeocodingResponse** response) {
    /* Build URL */
    char* url = geocoding_build_api_url(city_name, country,
                                        g_config.max_results,
                                        g_config.language);
    if (!url) {
        return -1;
    }

    printf("[GEOCODING] Fetching: %s\n", url);

    char* response_data = NULL;
    int   http_status   = 0;

    int result =
        geocoding_fetch_url_sync(url, &response_data, &http_status);
    free(url);

    if (result != 0 || !response_data) {
        return -2;
    }

    /* Parse JSON */
    int parse_result = geocoding_parse_json(response_data, response);

    free(response_data);

    if (parse_result != 0) {
        return -3;
    }

    printf("[GEOCODING] Found %d result(s)\n", (*response)->count);
    return 0;
}
