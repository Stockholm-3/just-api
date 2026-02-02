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

#include <http_utils.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

    return open_meteo_api_init(&config);
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
void open_meteo_handler_cleanup(void) { open_meteo_api_cleanup(); }

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
