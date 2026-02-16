/**
 * @file open_meteo_handler.c
 * @brief Implementation of HTTP endpoint handler for Open-Meteo weather API.
 *
 * This file implements the HTTP request handling logic for weather endpoints.
 * It processes incoming requests, interacts with the Open-Meteo API client,
 * and formats responses using the standardized response builder.
 *
 * @see open_meteo_handler.h for the public interface
 */

#include "open_meteo_handler.h"

#include "open_meteo_api.h"
#include "response_builder.h"

#include <cache_utils/file_cache.h>
#include <http_client.h>
#include <http_utils.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============= Hourly Cache ============= */

#define HOURLY_CACHE_DIR "./cache/weather_cache/hourly"
#define HOURLY_CACHE_TTL 3600 /* 1 hour */

static FileCacheInstance* g_hourly_cache = NULL;

/* ============= Minutely Cache ============= */

#define MINUTELY_CACHE_DIR "./cache/weather_cache/minutely"
#define MINUTELY_CACHE_TTL 900 /* 15 minutes */

static FileCacheInstance* g_minutely_cache = NULL;

/* ============= Internal Structures ============= */

typedef struct {
    HTTPServerConnection* conn;
    float                 lat;
    float                 lon;
} AsyncHandlerContext;

/* ============= Internal Function Declarations ============= */

static void weather_fetch_callback(int status, WeatherData* data,
                                   void* context);

/**
 * @brief Initialize the Open-Meteo handler module.
 *
 * Configures and initializes the Open-Meteo API client with caching enabled.
 *
 * @return 0 on success, non-zero on failure.
 */
int open_meteo_handler_init(void) {
    WeatherConfig config = {.cache_dir = "./cache/weather_cache",
                            .cache_ttl = 900, /* 15 minutes */
                            .use_cache = true};

    int result = open_meteo_api_init(&config);
    if (result != 0) {
        return result;
    }

    /* Initialize hourly cache */
    FileCacheConfig hourly_cache_cfg = {.cache_dir   = HOURLY_CACHE_DIR,
                                        .ttl_seconds = HOURLY_CACHE_TTL,
                                        .enabled     = true};
    g_hourly_cache                   = file_cache_create(&hourly_cache_cfg);
    if (!g_hourly_cache) {
        fprintf(stderr, "[METEO] Warning: Failed to initialize hourly cache\n");
    } else {
        printf("[METEO] Hourly cache initialized (TTL: %d seconds)\n",
               HOURLY_CACHE_TTL);
    }

    /* Initialize minutely cache */
    FileCacheConfig minutely_cache_cfg = {.cache_dir   = MINUTELY_CACHE_DIR,
                                          .ttl_seconds = MINUTELY_CACHE_TTL,
                                          .enabled     = true};
    g_minutely_cache                   = file_cache_create(&minutely_cache_cfg);
    if (!g_minutely_cache) {
        fprintf(stderr,
                "[METEO] Warning: Failed to initialize minutely cache\n");
    } else {
        printf("[METEO] Minutely cache initialized (TTL: %d seconds)\n",
               MINUTELY_CACHE_TTL);
    }

    return 0;
}

/**
 * @brief Handle GET /v1/current endpoint request.
 *
 * Parses latitude and longitude from query string, fetches current weather
 * data from Open-Meteo API, and builds a JSON response with weather details.
 *
 * @param[in]  query_string  URL query parameters containing lat and lon.
 * @param[out] response_json Allocated JSON response string (caller frees).
 * @param[out] status_code   HTTP status code for the response.
 *
 * @return 0 on success, -1 on error.
 */
int open_meteo_handler_current(const char* query_string, char** response_json,
                               int* status_code) {
    if (!response_json || !status_code) {
        return -1;
    }

    *response_json = NULL;
    *status_code   = HTTP_INTERNAL_ERROR;

    /* Parse query parameters */
    float lat, lon;
    if (open_meteo_api_parse_query(query_string, &lat, &lon) != 0) {
        *response_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Invalid query parameters. Expected format: "
            "lat=XX.XXXX&lon=YY.YYYY");
        *status_code = HTTP_BAD_REQUEST;
        return -1;
    }

    /* Create location */
    Location location = {
        .latitude = lat, .longitude = lon, .name = "Query Location"};

    /* Get current weather */
    WeatherData* weather_data = NULL;
    int          result = open_meteo_api_get_current(&location, &weather_data);

    if (result != 0 || !weather_data) {
        *response_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to fetch weather data from Open-Meteo API");
        *status_code = HTTP_INTERNAL_ERROR;
        return -1;
    }

    /* Build structured JSON response */
    json_t* data = json_object();

    /* Weather data - add first (order matches documentation) */
    json_t* weather_obj = json_object();
    json_object_set_new(weather_obj, "temperature",
                        json_real(weather_data->temperature));
    json_object_set_new(weather_obj, "temperature_unit",
                        json_string(weather_data->temperature_unit));
    json_object_set_new(weather_obj, "windspeed",
                        json_real(weather_data->windspeed));
    json_object_set_new(weather_obj, "windspeed_unit",
                        json_string(weather_data->windspeed_unit));
    json_object_set_new(weather_obj, "wind_direction_10m",
                        json_integer(weather_data->winddirection));
    json_object_set_new(weather_obj, "wind_direction_name",
                        json_string(open_meteo_api_get_wind_direction(
                            weather_data->winddirection)));
    json_object_set_new(weather_obj, "weather_code",
                        json_integer(weather_data->weather_code));
    json_object_set_new(weather_obj, "weather_description",
                        json_string(open_meteo_api_get_description(
                            weather_data->weather_code)));
    json_object_set_new(weather_obj, "is_day",
                        json_integer(weather_data->is_day ? 1 : 0));
    json_object_set_new(weather_obj, "precipitation",
                        json_real(weather_data->precipitation));
    json_object_set_new(weather_obj, "precipitation_unit", json_string("mm"));
    json_object_set_new(weather_obj, "humidity",
                        json_real(weather_data->humidity));
    json_object_set_new(weather_obj, "pressure",
                        json_real(weather_data->pressure));

    /* Format time as "YYYY-MM-DDTHH:MM" */
    time_t     now     = time(NULL);
    struct tm* tm_info = localtime(&now);
    char       time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M", tm_info);
    json_object_set_new(weather_obj, "time", json_string(time_str));

    json_object_set_new(data, "current_weather", weather_obj);

    /* Location information - add last */
    json_t* location_obj = json_object();
    json_object_set_new(location_obj, "latitude", json_real(lat));
    json_object_set_new(location_obj, "longitude", json_real(lon));
    json_object_set_new(data, "location", location_obj);

    /* Cleanup weather data */
    open_meteo_api_free_current(weather_data);

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
 * @brief Clean up the Open-Meteo handler module.
 *
 * Releases all resources allocated by the underlying Open-Meteo API client.
 */
void open_meteo_handler_cleanup(void) {
    open_meteo_api_cleanup();

    if (g_hourly_cache) {
        file_cache_destroy(g_hourly_cache);
        g_hourly_cache = NULL;
    }

    if (g_minutely_cache) {
        file_cache_destroy(g_minutely_cache);
        g_minutely_cache = NULL;
    }
}

/* ============= Async Handler Implementation ============= */

/**
 * @brief Callback for async weather data retrieval.
 *
 * Called when weather data is fetched. Builds and sends the HTTP response.
 */
static void weather_fetch_callback(int status, WeatherData* data,
                                   void* context) {
    AsyncHandlerContext* ctx = (AsyncHandlerContext*)context;
    if (!ctx || !ctx->conn) {
        free(ctx);
        return;
    }

    if (status != 0 || !data) {
        /* Error fetching weather data */
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to fetch weather data from Open-Meteo API");

        if (error_json) {
            send_response(ctx->conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        free(ctx);
        return;
    }

    /* Build structured JSON response */
    json_t* response_data = json_object();

    /* Weather data */
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

    /* Format time */
    time_t     now     = time(NULL);
    struct tm* tm_info = localtime(&now);
    char       time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M", tm_info);
    json_object_set_new(weather_obj, "time", json_string(time_str));

    json_object_set_new(response_data, "current_weather", weather_obj);

    /* Location information */
    json_t* location_obj = json_object();
    json_object_set_new(location_obj, "latitude", json_real(ctx->lat));
    json_object_set_new(location_obj, "longitude", json_real(ctx->lon));
    json_object_set_new(response_data, "location", location_obj);

    /* Build standardized response */
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

    /* Cleanup */
    open_meteo_api_free_current(data);
    free(ctx);
}

/**
 * @brief Handle GET /v1/current endpoint request (async version).
 */
int open_meteo_handler_current_async(HTTPServerConnection* conn,
                                     const char*           query_string) {
    if (!conn) {
        return -1;
    }

    /* Parse query parameters */
    float lat, lon;
    if (open_meteo_api_parse_query(query_string, &lat, &lon) != 0) {
        char* error_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Invalid query parameters. Expected format: "
            "lat=XX.XXXX&lon=YY.YYYY");

        if (error_json) {
            send_response(conn, HTTP_BAD_REQUEST, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        return -1;
    }

    /* Create context for callback */
    AsyncHandlerContext* ctx = malloc(sizeof(AsyncHandlerContext));
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
        return -1;
    }

    ctx->conn = conn;
    ctx->lat  = lat;
    ctx->lon  = lon;

    /* Create location */
    Location location = {
        .latitude = lat, .longitude = lon, .name = "Query Location"};

    /* Get current weather async */
    int result = open_meteo_api_get_current_async(&location,
                                                  weather_fetch_callback, ctx);

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
        free(ctx);
        return -1;
    }

    return 0;
}

/* ============= Hourly Handler Implementation ============= */

#define HOURLY_API_BASE_URL "http://api.open-meteo.com/v1/forecast"

typedef struct {
    HTTPServerConnection* conn;
    float                 lat;
    float                 lon;
    int                   hours;
    char*                 cache_key;
} AsyncHourlyContext;

static void hourly_http_callback(const char* event, const char* response,
                                 void* context);

static char* build_hourly_url(float lat, float lon, int hours) {
    char* url = malloc(1024);
    if (!url)
        return NULL;

    snprintf(url, 1024,
             "%s?latitude=%.6f&longitude=%.6f"
             "&hourly=temperature_2m,relative_humidity_2m,precipitation,"
             "weather_code,surface_pressure,wind_speed_10m,wind_direction_10m,"
             "is_day&forecast_hours=%d&timezone=GMT",
             HOURLY_API_BASE_URL, lat, lon, hours);
    return url;
}

static int parse_hours_param(const char* query) {
    int   hours       = 24; /* default */
    char* hours_param = strstr(query, "hours=");
    if (hours_param) {
        hours = atoi(hours_param + 6);
        if (hours < 1)
            hours = 1;
        if (hours > 168)
            hours = 168;
    }
    return hours;
}

static char* build_hourly_response_json(const char* api_response, float lat,
                                        float lon) {
    json_error_t error;
    json_t* root = json_loadb(api_response, strlen(api_response), 0, &error);
    if (!root)
        return NULL;

    json_t* hourly       = json_object_get(root, "hourly");
    json_t* hourly_units = json_object_get(root, "hourly_units");
    if (!hourly) {
        json_decref(root);
        return NULL;
    }

    json_t* times = json_object_get(hourly, "time");
    size_t  count = json_array_size(times);
    if (count == 0) {
        json_decref(root);
        return NULL;
    }

    /* Get units */
    const char* temp_unit =
        json_string_value(json_object_get(hourly_units, "temperature_2m"));
    const char* wind_unit =
        json_string_value(json_object_get(hourly_units, "wind_speed_10m"));
    if (!temp_unit)
        temp_unit = "°C";
    if (!wind_unit)
        wind_unit = "km/h";

    /* Get arrays */
    json_t* temps    = json_object_get(hourly, "temperature_2m");
    json_t* humidity = json_object_get(hourly, "relative_humidity_2m");
    json_t* precip   = json_object_get(hourly, "precipitation");
    json_t* codes    = json_object_get(hourly, "weather_code");
    json_t* pressure = json_object_get(hourly, "surface_pressure");
    json_t* winds    = json_object_get(hourly, "wind_speed_10m");
    json_t* winddir  = json_object_get(hourly, "wind_direction_10m");
    json_t* isday    = json_object_get(hourly, "is_day");

    /* Build response */
    json_t* data         = json_object();
    json_t* location_obj = json_object();
    json_object_set_new(location_obj, "latitude", json_real(lat));
    json_object_set_new(location_obj, "longitude", json_real(lon));
    json_object_set_new(data, "location", location_obj);

    json_t* hourly_array = json_array();
    for (size_t i = 0; i < count; i++) {
        json_t* point = json_object();

        const char* time_str = json_string_value(json_array_get(times, i));
        json_object_set_new(point, "time",
                            json_string(time_str ? time_str : ""));

        json_object_set_new(
            point, "temperature",
            json_real(json_real_value(json_array_get(temps, i))));
        json_object_set_new(point, "temperature_unit", json_string(temp_unit));

        json_object_set_new(
            point, "humidity",
            json_real(json_real_value(json_array_get(humidity, i))));

        json_object_set_new(
            point, "precipitation",
            json_real(json_real_value(json_array_get(precip, i))));

        int code = json_integer_value(json_array_get(codes, i));
        json_object_set_new(point, "weather_code", json_integer(code));
        json_object_set_new(point, "weather_description",
                            json_string(open_meteo_api_get_description(code)));

        json_object_set_new(
            point, "windspeed",
            json_real(json_real_value(json_array_get(winds, i))));
        json_object_set_new(point, "windspeed_unit", json_string(wind_unit));

        int dir = json_integer_value(json_array_get(winddir, i));
        json_object_set_new(point, "wind_direction", json_integer(dir));
        json_object_set_new(
            point, "wind_direction_name",
            json_string(open_meteo_api_get_wind_direction(dir)));

        json_object_set_new(
            point, "pressure",
            json_real(json_real_value(json_array_get(pressure, i))));

        json_object_set_new(
            point, "is_day",
            json_integer(json_integer_value(json_array_get(isday, i))));

        json_array_append_new(hourly_array, point);
    }
    json_object_set_new(data, "hourly_forecast", hourly_array);

    json_decref(root);

    char* result = response_builder_success(data);
    return result;
}

static void hourly_http_callback(const char* event, const char* response,
                                 void* context) {
    AsyncHourlyContext* ctx = (AsyncHourlyContext*)context;
    if (!ctx || !ctx->conn) {
        if (ctx) {
            free(ctx->cache_key);
            free(ctx);
        }
        return;
    }

    if (strcmp(event, "RESPONSE") == 0 && response) {
        /* Save to cache */
        if (g_hourly_cache && ctx->cache_key) {
            json_error_t error;
            json_t* json = json_loadb(response, strlen(response), 0, &error);
            if (json) {
                file_cache_save_json(g_hourly_cache, ctx->cache_key, json);
                json_decref(json);
            }
        }

        /* Build response */
        char* json_response =
            build_hourly_response_json(response, ctx->lat, ctx->lon);

        if (json_response) {
            send_response(ctx->conn, HTTP_OK, "application/json", json_response,
                          strlen(json_response));
            free(json_response);
        } else {
            char* error_json = response_builder_error(
                HTTP_INTERNAL_ERROR,
                response_builder_get_error_type(HTTP_INTERNAL_ERROR),
                "Failed to parse hourly weather data");
            if (error_json) {
                send_response(ctx->conn, HTTP_INTERNAL_ERROR,
                              "application/json", error_json,
                              strlen(error_json));
                free(error_json);
            }
        }
    } else {
        /* Error or timeout */
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to fetch hourly weather data");
        if (error_json) {
            send_response(ctx->conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
    }

    free(ctx->cache_key);
    free(ctx);
}

int open_meteo_handler_hourly(const char* query_string, char** response_json,
                              int* status_code) {
    /* This sync version only works with cache */
    if (!response_json || !status_code)
        return -1;

    *response_json = NULL;
    *status_code   = HTTP_INTERNAL_ERROR;

    float lat, lon;
    if (open_meteo_api_parse_query(query_string, &lat, &lon) != 0) {
        *response_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Invalid query parameters. Expected: lat=XX&lon=YY");
        *status_code = HTTP_BAD_REQUEST;
        return -1;
    }

    int hours = parse_hours_param(query_string);

    /* Generate cache key */
    char key_input[256];
    snprintf(key_input, sizeof(key_input), "hourly_%.6f_%.6f_%d", lat, lon,
             hours);

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (!g_hourly_cache ||
        file_cache_generate_key(g_hourly_cache, key_input, cache_key,
                                sizeof(cache_key)) != FILE_CACHE_OK) {
        *response_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Cache not initialized");
        *status_code = HTTP_INTERNAL_ERROR;
        return -1;
    }

    /* Check cache */
    if (file_cache_is_valid(g_hourly_cache, cache_key)) {
        printf("[METEO] Hourly sync cache HIT\n");
        json_t* cached_json = NULL;
        if (file_cache_load_json(g_hourly_cache, cache_key,
                                 (void**)&cached_json) == FILE_CACHE_OK) {
            char* api_str = json_dumps(cached_json, 0);
            json_decref(cached_json);

            if (api_str) {
                *response_json = build_hourly_response_json(api_str, lat, lon);
                free(api_str);

                if (*response_json) {
                    *status_code = HTTP_OK;
                    return 0;
                }
            }
        }
    }

    /* Cache miss - sync version cannot fetch from API */
    printf("[METEO] Hourly sync cache MISS\n");
    *response_json = response_builder_error(
        HTTP_INTERNAL_ERROR,
        response_builder_get_error_type(HTTP_INTERNAL_ERROR),
        "Hourly data not in cache. Please try again.");
    *status_code = HTTP_INTERNAL_ERROR;
    return -1;
}

int open_meteo_handler_hourly_async(HTTPServerConnection* conn,
                                    const char*           query_string) {
    if (!conn)
        return -1;

    float lat, lon;
    if (open_meteo_api_parse_query(query_string, &lat, &lon) != 0) {
        char* error_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Invalid query parameters. Expected: lat=XX&lon=YY");
        if (error_json) {
            send_response(conn, HTTP_BAD_REQUEST, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        return -1;
    }

    int hours = parse_hours_param(query_string);

    /* Generate cache key */
    char key_input[256];
    snprintf(key_input, sizeof(key_input), "hourly_%.6f_%.6f_%d", lat, lon,
             hours);

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (g_hourly_cache &&
        file_cache_generate_key(g_hourly_cache, key_input, cache_key,
                                sizeof(cache_key)) == FILE_CACHE_OK) {
        /* Check cache */
        if (file_cache_is_valid(g_hourly_cache, cache_key)) {
            printf("[METEO] Hourly cache HIT\n");
            json_t* cached_json = NULL;
            if (file_cache_load_json(g_hourly_cache, cache_key,
                                     (void**)&cached_json) == FILE_CACHE_OK) {
                char* api_str = json_dumps(cached_json, 0);
                json_decref(cached_json);

                if (api_str) {
                    char* response_json =
                        build_hourly_response_json(api_str, lat, lon);
                    free(api_str);

                    if (response_json) {
                        send_response(conn, HTTP_OK, "application/json",
                                      response_json, strlen(response_json));
                        free(response_json);
                        return 0;
                    }
                }
            }
        }
    }

    printf("[METEO] Hourly cache MISS - fetching from API\n");

    /* Create async context */
    AsyncHourlyContext* ctx = malloc(sizeof(AsyncHourlyContext));
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
        return -1;
    }

    ctx->conn      = conn;
    ctx->lat       = lat;
    ctx->lon       = lon;
    ctx->hours     = hours;
    ctx->cache_key = strdup(cache_key);

    /* Build URL and fetch */
    char* url = build_hourly_url(lat, lon, hours);
    if (!url) {
        free(ctx->cache_key);
        free(ctx);
        return -1;
    }

    printf("[METEO] Fetching hourly: %s\n", url);

    int result = http_client_get(url, NULL, 30000, hourly_http_callback, ctx);
    free(url);

    if (result != 0) {
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to initiate hourly data fetch");
        if (error_json) {
            send_response(conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        free(ctx->cache_key);
        free(ctx);
        return -1;
    }

    return 0;
}

/* ============= Minutely Handler Implementation ============= */

typedef struct {
    HTTPServerConnection* conn;
    float                 lat;
    float                 lon;
    int                   steps;
    char*                 cache_key;
} AsyncMinutelyContext;

static void minutely_http_callback(const char* event, const char* response,
                                   void* context);

static char* build_minutely_url(float lat, float lon, int steps) {
    char* url = malloc(1024);
    if (!url)
        return NULL;

    snprintf(url, 1024,
             "%s?latitude=%.6f&longitude=%.6f"
             "&minutely_15=temperature_2m,relative_humidity_2m,precipitation,"
             "weather_code,surface_pressure,wind_speed_10m,wind_direction_10m,"
             "is_day&forecast_minutely_15=%d&timezone=GMT",
             HOURLY_API_BASE_URL, lat, lon, steps);
    return url;
}

static char* build_minutely_response_json(const char* api_response, float lat,
                                          float lon) {
    json_error_t error;
    json_t* root = json_loadb(api_response, strlen(api_response), 0, &error);
    if (!root)
        return NULL;

    json_t* minutely       = json_object_get(root, "minutely_15");
    json_t* minutely_units = json_object_get(root, "minutely_15_units");
    if (!minutely) {
        json_decref(root);
        return NULL;
    }

    json_t* times = json_object_get(minutely, "time");
    size_t  count = json_array_size(times);
    if (count == 0) {
        json_decref(root);
        return NULL;
    }

    /* Get units */
    const char* temp_unit =
        json_string_value(json_object_get(minutely_units, "temperature_2m"));
    const char* wind_unit =
        json_string_value(json_object_get(minutely_units, "wind_speed_10m"));
    if (!temp_unit)
        temp_unit = "°C";
    if (!wind_unit)
        wind_unit = "km/h";

    /* Get arrays */
    json_t* temps    = json_object_get(minutely, "temperature_2m");
    json_t* humidity = json_object_get(minutely, "relative_humidity_2m");
    json_t* precip   = json_object_get(minutely, "precipitation");
    json_t* codes    = json_object_get(minutely, "weather_code");
    json_t* pressure = json_object_get(minutely, "surface_pressure");
    json_t* winds    = json_object_get(minutely, "wind_speed_10m");
    json_t* winddir  = json_object_get(minutely, "wind_direction_10m");
    json_t* isday    = json_object_get(minutely, "is_day");

    /* Build response */
    json_t* data         = json_object();
    json_t* location_obj = json_object();
    json_object_set_new(location_obj, "latitude", json_real(lat));
    json_object_set_new(location_obj, "longitude", json_real(lon));
    json_object_set_new(data, "location", location_obj);

    json_t* minutely_array = json_array();
    for (size_t i = 0; i < count; i++) {
        json_t* point = json_object();

        const char* time_str = json_string_value(json_array_get(times, i));
        json_object_set_new(point, "time",
                            json_string(time_str ? time_str : ""));

        json_object_set_new(
            point, "temperature",
            json_real(json_real_value(json_array_get(temps, i))));
        json_object_set_new(point, "temperature_unit", json_string(temp_unit));

        json_object_set_new(
            point, "humidity",
            json_real(json_real_value(json_array_get(humidity, i))));

        json_object_set_new(
            point, "precipitation",
            json_real(json_real_value(json_array_get(precip, i))));

        int code = json_integer_value(json_array_get(codes, i));
        json_object_set_new(point, "weather_code", json_integer(code));
        json_object_set_new(point, "weather_description",
                            json_string(open_meteo_api_get_description(code)));

        json_object_set_new(
            point, "windspeed",
            json_real(json_real_value(json_array_get(winds, i))));
        json_object_set_new(point, "windspeed_unit", json_string(wind_unit));

        int dir = json_integer_value(json_array_get(winddir, i));
        json_object_set_new(point, "wind_direction", json_integer(dir));
        json_object_set_new(
            point, "wind_direction_name",
            json_string(open_meteo_api_get_wind_direction(dir)));

        json_object_set_new(
            point, "pressure",
            json_real(json_real_value(json_array_get(pressure, i))));

        json_object_set_new(
            point, "is_day",
            json_integer(json_integer_value(json_array_get(isday, i))));

        json_array_append_new(minutely_array, point);
    }
    json_object_set_new(data, "minutely_forecast", minutely_array);

    json_decref(root);

    char* result = response_builder_success(data);
    return result;
}

static void minutely_http_callback(const char* event, const char* response,
                                   void* context) {
    AsyncMinutelyContext* ctx = (AsyncMinutelyContext*)context;
    if (!ctx || !ctx->conn) {
        if (ctx) {
            free(ctx->cache_key);
            free(ctx);
        }
        return;
    }

    if (strcmp(event, "RESPONSE") == 0 && response) {
        /* Save to cache */
        if (g_minutely_cache && ctx->cache_key) {
            json_error_t error;
            json_t* json = json_loadb(response, strlen(response), 0, &error);
            if (json) {
                file_cache_save_json(g_minutely_cache, ctx->cache_key, json);
                json_decref(json);
            }
        }

        /* Build response */
        char* json_response =
            build_minutely_response_json(response, ctx->lat, ctx->lon);

        if (json_response) {
            send_response(ctx->conn, HTTP_OK, "application/json", json_response,
                          strlen(json_response));
            free(json_response);
        } else {
            char* error_json = response_builder_error(
                HTTP_INTERNAL_ERROR,
                response_builder_get_error_type(HTTP_INTERNAL_ERROR),
                "Failed to parse minutely weather data");
            if (error_json) {
                send_response(ctx->conn, HTTP_INTERNAL_ERROR,
                              "application/json", error_json,
                              strlen(error_json));
                free(error_json);
            }
        }
    } else {
        /* Error or timeout */
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to fetch minutely weather data");
        if (error_json) {
            send_response(ctx->conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
    }

    free(ctx->cache_key);
    free(ctx);
}

int open_meteo_handler_minutely(const char* query_string, char** response_json,
                                int* status_code) {
    if (!response_json || !status_code)
        return -1;

    *response_json = NULL;
    *status_code   = HTTP_INTERNAL_ERROR;

    float lat, lon;
    if (open_meteo_api_parse_query(query_string, &lat, &lon) != 0) {
        *response_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Invalid query parameters. Expected: lat=XX&lon=YY");
        *status_code = HTTP_BAD_REQUEST;
        return -1;
    }

    int hours = parse_hours_param(query_string);
    int steps = hours * 4;

    /* Generate cache key */
    char key_input[256];
    snprintf(key_input, sizeof(key_input), "minutely_%.6f_%.6f_%d", lat, lon,
             steps);

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (!g_minutely_cache ||
        file_cache_generate_key(g_minutely_cache, key_input, cache_key,
                                sizeof(cache_key)) != FILE_CACHE_OK) {
        *response_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Cache not initialized");
        *status_code = HTTP_INTERNAL_ERROR;
        return -1;
    }

    /* Check cache */
    if (file_cache_is_valid(g_minutely_cache, cache_key)) {
        printf("[METEO] Minutely sync cache HIT\n");
        json_t* cached_json = NULL;
        if (file_cache_load_json(g_minutely_cache, cache_key,
                                 (void**)&cached_json) == FILE_CACHE_OK) {
            char* api_str = json_dumps(cached_json, 0);
            json_decref(cached_json);

            if (api_str) {
                *response_json =
                    build_minutely_response_json(api_str, lat, lon);
                free(api_str);

                if (*response_json) {
                    *status_code = HTTP_OK;
                    return 0;
                }
            }
        }
    }

    /* Cache miss - sync version cannot fetch from API */
    printf("[METEO] Minutely sync cache MISS\n");
    *response_json = response_builder_error(
        HTTP_INTERNAL_ERROR,
        response_builder_get_error_type(HTTP_INTERNAL_ERROR),
        "Minutely data not in cache. Please try again.");
    *status_code = HTTP_INTERNAL_ERROR;
    return -1;
}

int open_meteo_handler_minutely_async(HTTPServerConnection* conn,
                                      const char*           query_string) {
    if (!conn)
        return -1;

    float lat, lon;
    if (open_meteo_api_parse_query(query_string, &lat, &lon) != 0) {
        char* error_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Invalid query parameters. Expected: lat=XX&lon=YY");
        if (error_json) {
            send_response(conn, HTTP_BAD_REQUEST, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        return -1;
    }

    int hours = parse_hours_param(query_string);
    int steps = hours * 4;

    /* Generate cache key */
    char key_input[256];
    snprintf(key_input, sizeof(key_input), "minutely_%.6f_%.6f_%d", lat, lon,
             steps);

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (g_minutely_cache &&
        file_cache_generate_key(g_minutely_cache, key_input, cache_key,
                                sizeof(cache_key)) == FILE_CACHE_OK) {
        /* Check cache */
        if (file_cache_is_valid(g_minutely_cache, cache_key)) {
            printf("[METEO] Minutely cache HIT\n");
            json_t* cached_json = NULL;
            if (file_cache_load_json(g_minutely_cache, cache_key,
                                     (void**)&cached_json) == FILE_CACHE_OK) {
                char* api_str = json_dumps(cached_json, 0);
                json_decref(cached_json);

                if (api_str) {
                    char* response_json =
                        build_minutely_response_json(api_str, lat, lon);
                    free(api_str);

                    if (response_json) {
                        send_response(conn, HTTP_OK, "application/json",
                                      response_json, strlen(response_json));
                        free(response_json);
                        return 0;
                    }
                }
            }
        }
    }

    printf("[METEO] Minutely cache MISS - fetching from API\n");

    /* Create async context */
    AsyncMinutelyContext* ctx = malloc(sizeof(AsyncMinutelyContext));
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
        return -1;
    }

    ctx->conn      = conn;
    ctx->lat       = lat;
    ctx->lon       = lon;
    ctx->steps     = steps;
    ctx->cache_key = strdup(cache_key);

    /* Build URL and fetch */
    char* url = build_minutely_url(lat, lon, steps);
    if (!url) {
        free(ctx->cache_key);
        free(ctx);
        return -1;
    }

    printf("[METEO] Fetching minutely: %s\n", url);

    int result = http_client_get(url, NULL, 30000, minutely_http_callback, ctx);
    free(url);

    if (result != 0) {
        char* error_json = response_builder_error(
            HTTP_INTERNAL_ERROR,
            response_builder_get_error_type(HTTP_INTERNAL_ERROR),
            "Failed to initiate minutely data fetch");
        if (error_json) {
            send_response(conn, HTTP_INTERNAL_ERROR, "application/json",
                          error_json, strlen(error_json));
            free(error_json);
        }
        free(ctx->cache_key);
        free(ctx);
        return -1;
    }

    return 0;
}
