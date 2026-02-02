/**
 * @file weather_location_handler.c
 * @brief Implementation of the combined geocoding and weather handler.
 *
 * This file implements the weather location handler which provides
 * city-based weather lookups and city search functionality.
 *
 * @see weather_location_handler.h for the public interface
 */

#include "weather_location_handler.h"

#include "geocoding_api.h"
#include "open_meteo_api.h"
#include "open_meteo_handler.h"
#include "popular_cities.h"
#include "response_builder.h"

#include <http_utils.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============= Internal Structures ============= */

typedef struct {
    HTTPServerConnection* conn;
    GeocodingResponse*    geo_response;
    GeocodingResult*      best_location;
} AsyncWeatherLocationContext;

/**
 * @brief Global flag indicating whether the module has been initialized.
 * @internal
 */
static bool g_initialized = false;

/**
 * @brief Pointer to the popular cities database for fast lookups.
 * @internal
 */
static PopularCitiesDB* g_wlh_popular_cities_db = NULL;

/**
 * @brief External reference to geocoding API's global popular cities DB
 * pointer.
 * @internal
 *
 * This allows the geocoding module to use our loaded database.
 */
extern void* g_popular_cities_db;

/* Forward declarations for internal functions */
static void url_decode(const char* src, char* dst, size_t dst_size);
static int  parse_city_query(const char* query, char* city, size_t city_size,
                             char* country, size_t country_size, char* region,
                             size_t region_size);
static int  ensure_initialized(void);

/* ============= Lazy Initialization ============= */

/**
 * @brief Ensure all dependent modules are initialized.
 * @internal
 *
 * Performs lazy initialization of weather API, geocoding API,
 * and popular cities database. Safe to call multiple times.
 *
 * @return 0 on success, -1 on failure.
 */
static int ensure_initialized(void) {
    if (g_initialized) {
        return 0; /* Already initialized */
    }

    printf("[WEATHER_LOCATION] Initializing modules...\n");

    /* FIX: Initialize Weather API FIRST */
    /* This will create ./cache/ and set up caching */
    if (open_meteo_handler_init() != 0) {
        fprintf(stderr, "[WEATHER_LOCATION] Failed to init weather API\n");
        return -1;
    }

    /* Initialize geocoding API */
    GeocodingConfig geo_config = {.cache_dir   = "./cache/geo_cache",
                                  .cache_ttl   = 604800, /* 7 days */
                                  .use_cache   = true,
                                  .max_results = 10,
                                  .language    = "eng"};

    if (geocoding_api_init(&geo_config) != 0) {
        fprintf(stderr, "[WEATHER_LOCATION] Failed to init geocoding API\n");
        return -1;
    }

    /* Load popular cities database */
    int cities_result =
        popular_cities_load("./data/hot_cities.json", "./data/all_cities.json",
                            &g_wlh_popular_cities_db);

    if (cities_result != 0) {
        fprintf(stderr,
                "[WEATHER_LOCATION] Warning: Failed to load popular cities "
                "database (fallback to API-only mode)\n");
        /* Not a critical error - continue without local database */
        g_popular_cities_db = NULL;
    } else {
        printf("[WEATHER_LOCATION] Loaded popular cities database\n");
        /* Set the global pointer for geocoding_api to use */
        g_popular_cities_db = g_wlh_popular_cities_db;
    }

    g_initialized = true;
    printf("[WEATHER_LOCATION] All modules initialized successfully\n");
    return 0;
}

/* ============= Public API ============= */

/**
 * @brief Initialize the weather location handler explicitly.
 *
 * @return 0 on success, non-zero on failure.
 */
int weather_location_handler_init(void) {
    /* Explicit initialization (optional) */
    return ensure_initialized();
}

/**
 * @brief Handle weather request by city name.
 *
 * @param[in]  query_string  URL query parameters.
 * @param[out] response_json Allocated JSON response (caller frees).
 * @param[out] status_code   HTTP status code.
 *
 * @return 0 on success, -1 on error.
 */
int weather_location_handler_by_city(const char* query_string,
                                     char** response_json, int* status_code) {
    if (!response_json || !status_code) {
        return -1;
    }

    /* Automatic initialization on first call */
    if (ensure_initialized() != 0) {
        *response_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to initialize geocoding module");
        *status_code = HTTP_INTERNAL_ERROR;
        return -1;
    }

    *response_json = NULL;
    *status_code   = HTTP_INTERNAL_ERROR;

    /* Parse query parameters */
    char city[128]  = {0};
    char country[8] = {0};
    char region[64] = {0};

    if (parse_city_query(query_string, city, sizeof(city), country,
                         sizeof(country), region, sizeof(region)) != 0) {
        *response_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Invalid query parameters. Expected: city=<name>&country=<code>");
        *status_code = HTTP_BAD_REQUEST;
        return -1;
    }

    if (city[0] == '\0') {
        *response_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Missing required parameter: city");
        *status_code = HTTP_BAD_REQUEST;
        return -1;
    }

    printf("[WEATHER_LOCATION] Request for city: %s%s%s%s%s\n", city,
           region[0] ? ", " : "", region, country[0] ? " (" : "",
           country[0] ? country : "");

    /* 1. Find city coordinates via geocoding */
    GeocodingResponse* geo_response = NULL;
    int                result;

    if (region[0] != '\0') {
        result = geocoding_api_search_detailed(
            city, region, country[0] ? country : NULL, &geo_response);
    } else {
        result = geocoding_api_search(city, country[0] ? country : NULL,
                                      &geo_response);
    }

    if (result != 0 || !geo_response || geo_response->count == 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "City not found: %s", city);
        *response_json = response_builder_error(
            HTTP_NOT_FOUND, response_builder_get_error_type(HTTP_NOT_FOUND),
            error_msg);
        *status_code = HTTP_NOT_FOUND;

        if (geo_response) {
            geocoding_api_free_response(geo_response);
        }
        return -1;
    }

    /* Take the best result */
    GeocodingResult* best_location = geocoding_api_get_best_result(
        geo_response, country[0] ? country : NULL);
    if (!best_location) {
        *response_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to determine best location");
        *status_code = HTTP_INTERNAL_ERROR;
        geocoding_api_free_response(geo_response);
        return -1;
    }

    printf("[WEATHER_LOCATION] Found: %s, %s (%.4f, %.4f)\n",
           best_location->name, best_location->country, best_location->latitude,
           best_location->longitude);

    /* 2. Fetch weather for the found coordinates */
    Location location = {.latitude  = best_location->latitude,
                         .longitude = best_location->longitude,
                         .name      = best_location->name};

    WeatherData* weather_data = NULL;
    result = open_meteo_api_get_current(&location, &weather_data);

    if (result != 0 || !weather_data) {
        *response_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to fetch weather data");
        *status_code = HTTP_INTERNAL_ERROR;
        geocoding_api_free_response(geo_response);
        return -1;
    }

    /* 3. Build JSON response with city and weather information */
    json_t* data = json_object();

    /* Add location information */
    json_t* location_obj = json_object();
    json_object_set_new(location_obj, "name", json_string(best_location->name));
    json_object_set_new(location_obj, "country",
                        json_string(best_location->country));
    json_object_set_new(location_obj, "country_code",
                        json_string(best_location->country_code));

    if (best_location->admin1[0]) {
        json_object_set_new(location_obj, "region",
                            json_string(best_location->admin1));
    }

    json_object_set_new(location_obj, "latitude",
                        json_real(best_location->latitude));
    json_object_set_new(location_obj, "longitude",
                        json_real(best_location->longitude));

    if (best_location->population > 0) {
        json_object_set_new(location_obj, "population",
                            json_integer(best_location->population));
    }

    if (best_location->timezone[0]) {
        json_object_set_new(location_obj, "timezone",
                            json_string(best_location->timezone));
    }

    json_object_set_new(data, "location", location_obj);

    /* Add weather data */
    json_t* weather_obj = json_object();
    json_object_set_new(weather_obj, "temperature",
                        json_real(weather_data->temperature));
    json_object_set_new(weather_obj, "temperature_unit",
                        json_string(weather_data->temperature_unit));
    json_object_set_new(weather_obj, "weather_code",
                        json_integer(weather_data->weather_code));
    json_object_set_new(weather_obj, "weather_description",
                        json_string(open_meteo_api_get_description(
                            weather_data->weather_code)));
    json_object_set_new(weather_obj, "windspeed",
                        json_real(weather_data->windspeed));
    json_object_set_new(weather_obj, "windspeed_unit",
                        json_string(weather_data->windspeed_unit));
    json_object_set_new(weather_obj, "wind_direction_10m",
                        json_integer(weather_data->winddirection));
    json_object_set_new(weather_obj, "wind_direction_name",
                        json_string(open_meteo_api_get_wind_direction(
                            weather_data->winddirection)));
    json_object_set_new(weather_obj, "humidity",
                        json_real(weather_data->humidity));
    json_object_set_new(weather_obj, "pressure",
                        json_real(weather_data->pressure));
    json_object_set_new(weather_obj, "precipitation",
                        json_real(weather_data->precipitation));
    json_object_set_new(weather_obj, "is_day",
                        json_integer(weather_data->is_day ? 1 : 0));

    json_object_set_new(data, "current_weather", weather_obj);

    /* Cleanup weather data */
    open_meteo_api_free_current(weather_data);
    geocoding_api_free_response(geo_response);

    /* Build standardized response */
    *response_json = response_builder_success(data);

    if (!*response_json) {
        json_decref(data);
        *status_code = HTTP_INTERNAL_ERROR;
        return -1;
    }

    *status_code = HTTP_OK;
    printf("[WEATHER_LOCATION] Response generated successfully\n");
    return 0;
}

/**
 * @brief Handle city search request for autocomplete.
 *
 * @param[in]  query_string  URL query parameters with search query.
 * @param[out] response_json Allocated JSON response (caller frees).
 * @param[out] status_code   HTTP status code.
 *
 * @return 0 on success, -1 on error.
 */
int weather_location_handler_search_cities(const char* query_string,
                                           char**      response_json,
                                           int*        status_code) {
    if (!response_json || !status_code) {
        return -1;
    }

    /* Automatic initialization on first call */
    if (ensure_initialized() != 0) {
        *response_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to initialize geocoding module");
        *status_code = HTTP_INTERNAL_ERROR;
        return -1;
    }

    *response_json = NULL;
    *status_code   = HTTP_INTERNAL_ERROR;

    /* Parse query parameter */
    char query[256] = {0};
    if (sscanf(query_string, "query=%255[^&]", query) != 1 ||
        query[0] == '\0') {
        *response_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Missing required parameter: query");
        *status_code = HTTP_BAD_REQUEST;
        return -1;
    }

    /* URL decode the query */
    char decoded_query[256] = {0};
    url_decode(query, decoded_query, sizeof(decoded_query));

    /* Validate minimum query length (2 characters) */
    if (strlen(decoded_query) < 2) {
        *response_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Query must be at least 2 characters");
        *status_code = HTTP_BAD_REQUEST;
        return -1;
    }

    /* Search for cities using 3-tier strategy:
     * 1. Popular Cities DB (in-memory, fastest)
     * 2. File cache (fast)
     * 3. Open-Meteo API (slow, uses quota)
     */
    GeocodingResponse* response = NULL;
    int result = geocoding_api_search_smart(decoded_query, &response);

    if (result != 0 || !response) {
        *response_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to search cities");
        *status_code = HTTP_INTERNAL_ERROR;
        return -1;
    }

    /* Build JSON response */
    json_t* data = json_object();
    json_object_set_new(data, "query", json_string(decoded_query));
    json_object_set_new(data, "count", json_integer(response->count));

    json_t* cities_array = json_array();
    for (int i = 0; i < response->count; i++) {
        GeocodingResult* city     = &response->results[i];
        json_t*          city_obj = json_object();

        json_object_set_new(city_obj, "name", json_string(city->name));
        json_object_set_new(city_obj, "country", json_string(city->country));
        json_object_set_new(city_obj, "country_code",
                            json_string(city->country_code));

        if (city->admin1[0]) {
            json_object_set_new(city_obj, "region", json_string(city->admin1));
        }

        json_object_set_new(city_obj, "latitude", json_real(city->latitude));
        json_object_set_new(city_obj, "longitude", json_real(city->longitude));

        if (city->population > 0) {
            json_object_set_new(city_obj, "population",
                                json_integer(city->population));
        }

        json_array_append_new(cities_array, city_obj);
    }

    json_object_set_new(data, "cities", cities_array);

    geocoding_api_free_response(response);

    /* Build standardized response */
    *response_json = response_builder_success(data);

    if (!*response_json) {
        json_decref(data);
        *status_code = HTTP_INTERNAL_ERROR;
        return -1;
    }

    *status_code = HTTP_OK;
    return 0;
}

/**
 * @brief Clean up the weather location handler and all dependencies.
 */
void weather_location_handler_cleanup(void) {
    if (!g_initialized) {
        return;
    }

    geocoding_api_cleanup();
    open_meteo_handler_cleanup();

    /* Cleanup popular cities database */
    if (g_wlh_popular_cities_db) {
        popular_cities_free(g_wlh_popular_cities_db);
        g_wlh_popular_cities_db = NULL;
        g_popular_cities_db     = NULL;
    }

    g_initialized = false;
    printf("[WEATHER_LOCATION] Handler cleaned up\n");
}

/* ============= Internal Functions ============= */

/**
 * @brief Decode URL-encoded string.
 * @internal
 *
 * Converts %XX sequences to their character equivalents.
 * Also converts '+' and '_' to spaces for query parameter compatibility.
 *
 * @param[in]  src      Source URL-encoded string.
 * @param[out] dst      Destination buffer for decoded string.
 * @param[in]  dst_size Size of destination buffer.
 */
static void url_decode(const char* src, char* dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) {
        return;
    }

    size_t dst_pos = 0;
    for (size_t i = 0; src[i] && dst_pos + 1 < dst_size; i++) {
        if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            /* Parse hex value */
            char hex[3] = {src[i + 1], src[i + 2], '\0'};
            int  value  = (int)strtol(hex, NULL, 16);
            if (value > 0 && value < 256) {
                dst[dst_pos++] = (char)value;
                i += 2;
            } else {
                dst[dst_pos++] = src[i];
            }
        } else if (src[i] == '+' || src[i] == '_') {
            /* Both + and _ represent spaces in query parameters */
            dst[dst_pos++] = ' ';
        } else {
            dst[dst_pos++] = src[i];
        }
    }
    dst[dst_pos] = '\0';
}

/**
 * @brief Parse city query parameters from URL query string.
 * @internal
 *
 * Extracts city, country, and region parameters from a query string.
 * Parameters are URL-decoded during extraction.
 *
 * @param[in]  query        URL query string to parse.
 * @param[out] city         Buffer to receive city name.
 * @param[in]  city_size    Size of city buffer.
 * @param[out] country      Buffer to receive country code.
 * @param[in]  country_size Size of country buffer.
 * @param[out] region       Buffer to receive region name.
 * @param[in]  region_size  Size of region buffer.
 *
 * @return 0 if city parameter was found, -1 otherwise.
 */
static int parse_city_query(const char* query, char* city, size_t city_size,
                            char* country, size_t country_size, char* region,
                            size_t region_size) {
    if (!query || !city || !country || !region) {
        return -1;
    }

    /* Copy query for safe parsing */
    char query_copy[1024];
    strncpy(query_copy, query, sizeof(query_copy) - 1);
    query_copy[sizeof(query_copy) - 1] = '\0';

    /* Parse parameters: city=X&country=Y&region=Z */
    char* token      = strtok(query_copy, "&");
    int   found_city = 0;

    while (token != NULL) {
        if (strncmp(token, "city=", 5) == 0) {
            /* Decode URL-encoded city name */
            url_decode(token + 5, city, city_size);
            found_city = 1;
        } else if (strncmp(token, "country=", 8) == 0) {
            /* Decode URL-encoded country code */
            url_decode(token + 8, country, country_size);
        } else if (strncmp(token, "region=", 7) == 0) {
            /* Decode URL-encoded region */
            url_decode(token + 7, region, region_size);
        }
        token = strtok(NULL, "&");
    }

    return found_city ? 0 : -1;
}
/* ============= Async Handler Implementation ============= */

static void weather_by_city_callback(int status, WeatherData* data,
                                     void* context);

int weather_location_handler_by_city_async(HTTPServerConnection* conn,
                                           const char*           query_string) {
    if (!conn) {
        return -1;
    }

    if (ensure_initialized() != 0) {
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to initialize geocoding module");

        if (error_json) {
            send_response(conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        return -1;
    }

    char city[256]   = {0};
    char country[64] = {0};
    char region[256] = {0};

    if (parse_city_query(query_string, city, sizeof(city), country,
                         sizeof(country), region, sizeof(region)) != 0) {
        char* error_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Missing required parameter: city");

        if (error_json) {
            send_response(conn, HTTP_BAD_REQUEST, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        return -1;
    }

    printf("[WEATHER_LOCATION] Query: city='%s', country='%s', region='%s'\n",
           city, country, region);

    GeocodingResponse* geo_response = NULL;
    int                result =
        geocoding_api_search(city, country[0] ? country : NULL, &geo_response);

    if (result != 0 || !geo_response || geo_response->count == 0) {
        char* error_json = response_builder_error(
            geo_response && geo_response->count == 0 ? HTTP_NOT_FOUND
                                                     : HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(
                geo_response && geo_response->count == 0 ? HTTP_NOT_FOUND
                                                         : HTTP_INTERNAL_ERROR),
            geo_response && geo_response->count == 0
                ? "City not found"
                : "Failed to lookup city coordinates");

        if (error_json) {
            send_response(conn,
                          geo_response && geo_response->count == 0
                              ? HTTP_NOT_FOUND
                              : HTTP_INTERNAL_ERROR,
                          "application/json", error_json, strlen(error_json));
            free(error_json);
        }
        if (geo_response)
            geocoding_api_free_response(geo_response);
        return -1;
    }

    GeocodingResult* best_location = geocoding_api_get_best_result(
        geo_response, country[0] ? country : NULL);
    if (!best_location) {
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to determine best location");
        if (error_json) {
            send_response(conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        geocoding_api_free_response(geo_response);
        return -1;
    }

    printf("[WEATHER_LOCATION] Found: %s, %s (%.4f, %.4f)\n",
           best_location->name, best_location->country, best_location->latitude,
           best_location->longitude);

    AsyncWeatherLocationContext* ctx =
        malloc(sizeof(AsyncWeatherLocationContext));
    if (!ctx) {
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Memory allocation failed");
        if (error_json) {
            send_response(conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        geocoding_api_free_response(geo_response);
        return -1;
    }

    ctx->conn          = conn;
    ctx->geo_response  = geo_response;
    ctx->best_location = best_location;

    Location location = {.latitude  = best_location->latitude,
                         .longitude = best_location->longitude,
                         .name      = best_location->name};
    result            = open_meteo_api_get_current_async(&location,
                                                         weather_by_city_callback, ctx);

    if (result != 0) {
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to initiate weather data fetch");
        if (error_json) {
            send_response(conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        geocoding_api_free_response(geo_response);
        free(ctx);
        return -1;
    }

    return 0;
}

static void weather_by_city_callback(int status, WeatherData* data,
                                     void* context) {
    AsyncWeatherLocationContext* ctx = (AsyncWeatherLocationContext*)context;
    if (!ctx || !ctx->conn) {
        if (ctx) {
            if (ctx->geo_response)
                geocoding_api_free_response(ctx->geo_response);
            free(ctx);
        }
        return;
    }

    if (status != 0 || !data) {
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to fetch weather data");
        if (error_json) {
            send_response(ctx->conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        if (ctx->geo_response)
            geocoding_api_free_response(ctx->geo_response);
        free(ctx);
        return;
    }

    json_t* response_data = json_object();
    json_t* location_obj  = json_object();
    json_object_set_new(location_obj, "name",
                        json_string(ctx->best_location->name));
    json_object_set_new(location_obj, "country",
                        json_string(ctx->best_location->country));
    json_object_set_new(location_obj, "country_code",
                        json_string(ctx->best_location->country_code));
    if (ctx->best_location->admin1[0]) {
        json_object_set_new(location_obj, "region",
                            json_string(ctx->best_location->admin1));
    }
    json_object_set_new(location_obj, "latitude",
                        json_real(ctx->best_location->latitude));
    json_object_set_new(location_obj, "longitude",
                        json_real(ctx->best_location->longitude));
    if (ctx->best_location->population > 0) {
        json_object_set_new(location_obj, "population",
                            json_integer(ctx->best_location->population));
    }
    json_object_set_new(response_data, "location", location_obj);

    json_t* weather_obj = json_object();
    json_object_set_new(weather_obj, "temperature",
                        json_real(data->temperature));
    json_object_set_new(weather_obj, "temperature_unit",
                        json_string(data->temperature_unit));
    json_object_set_new(weather_obj, "windspeed", json_real(data->windspeed));
    json_object_set_new(weather_obj, "windspeed_unit",
                        json_string(data->windspeed_unit));
    json_object_set_new(weather_obj, "wind_direction_10m",
                        json_integer(data->winddirection));
    json_object_set_new(
        weather_obj, "wind_direction_name",
        json_string(open_meteo_api_get_wind_direction(data->winddirection)));
    json_object_set_new(weather_obj, "weather_code",
                        json_integer(data->weather_code));
    json_object_set_new(
        weather_obj, "weather_description",
        json_string(open_meteo_api_get_description(data->weather_code)));
    json_object_set_new(weather_obj, "is_day",
                        json_integer(data->is_day ? 1 : 0));
    json_object_set_new(weather_obj, "precipitation",
                        json_real(data->precipitation));
    json_object_set_new(weather_obj, "precipitation_unit", json_string("mm"));
    json_object_set_new(weather_obj, "humidity", json_real(data->humidity));
    json_object_set_new(weather_obj, "pressure", json_real(data->pressure));
    json_object_set_new(response_data, "current_weather", weather_obj);

    char* response_json = response_builder_success(response_data);
    if (response_json) {
        send_response(ctx->conn, HTTP_OK, "application/json", response_json,
                      strlen(response_json));
        free(response_json);
    } else {
        json_decref(response_data);
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to build response");
        if (error_json) {
            send_response(ctx->conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
    }

    open_meteo_api_free_current(data);
    if (ctx->geo_response)
        geocoding_api_free_response(ctx->geo_response);
    free(ctx);
}

/* ============= Hourly by City Implementation ============= */

static int parse_hours_query(const char* query) {
    int   hours       = 24; /* default */
    char* hours_param = strstr(query, "hours=");
    if (hours_param) {
        hours = atoi(hours_param + 6);
        if (hours < 1) hours = 1;
        if (hours > 168) hours = 168;
    }
    return hours;
}

int weather_location_handler_hourly_by_city_async(HTTPServerConnection* conn,
                                                  const char* query_string) {
    if (!conn) {
        return -1;
    }

    if (ensure_initialized() != 0) {
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to initialize geocoding module");

        if (error_json) {
            send_response(conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        return -1;
    }

    char city[256]   = {0};
    char country[64] = {0};
    char region[256] = {0};

    if (parse_city_query(query_string, city, sizeof(city), country,
                         sizeof(country), region, sizeof(region)) != 0) {
        char* error_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Missing required parameter: city");

        if (error_json) {
            send_response(conn, HTTP_BAD_REQUEST, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        return -1;
    }

    int hours = parse_hours_query(query_string);

    printf("[WEATHER_LOCATION] Hourly query: city='%s', country='%s', "
           "hours=%d\n",
           city, country, hours);

    /* Geocoding to find coordinates */
    GeocodingResponse* geo_response = NULL;
    int                result =
        geocoding_api_search(city, country[0] ? country : NULL, &geo_response);

    if (result != 0 || !geo_response || geo_response->count == 0) {
        char* error_json = response_builder_error(
            geo_response && geo_response->count == 0 ? HTTP_NOT_FOUND
                                                     : HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(
                geo_response && geo_response->count == 0 ? HTTP_NOT_FOUND
                                                         : HTTP_INTERNAL_ERROR),
            geo_response && geo_response->count == 0
                ? "City not found"
                : "Failed to lookup city coordinates");

        if (error_json) {
            send_response(conn,
                          geo_response && geo_response->count == 0
                              ? HTTP_NOT_FOUND
                              : HTTP_INTERNAL_ERROR,
                          "application/json", error_json, strlen(error_json));
            free(error_json);
        }
        if (geo_response)
            geocoding_api_free_response(geo_response);
        return -1;
    }

    GeocodingResult* best_location = geocoding_api_get_best_result(
        geo_response, country[0] ? country : NULL);
    if (!best_location) {
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to determine best location");
        if (error_json) {
            send_response(conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        geocoding_api_free_response(geo_response);
        return -1;
    }

    printf("[WEATHER_LOCATION] Found: %s, %s (%.4f, %.4f)\n",
           best_location->name, best_location->country, best_location->latitude,
           best_location->longitude);

    /* Build query for hourly handler with coordinates */
    char hourly_query[512];
    snprintf(hourly_query, sizeof(hourly_query), "lat=%.6f&lon=%.6f&hours=%d",
             best_location->latitude, best_location->longitude, hours);

    geocoding_api_free_response(geo_response);

    /* Delegate to hourly async handler */
    return open_meteo_handler_hourly_async(conn, hourly_query);
}
