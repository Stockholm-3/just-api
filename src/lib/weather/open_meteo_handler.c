/*  open_meteo_handler.c -  open_meteo_handler - endpoint handler for HTTP
 * server */

#include "open_meteo_handler.h"

#include "open_meteo_api.h"
#include "response_builder.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initialize weather server module */
int open_meteo_handler_init(void) {
    WeatherConfig config = {.cache_dir = "./cache/weather_cache",
                            .cache_ttl = 900, /* 15 minutes */
                            .use_cache = true};

    return open_meteo_api_init(&config);
}

/* Handle GET /v1/current endpoint */
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

    /* Location information */
    json_t* location_obj = json_object();
    json_object_set_new(location_obj, "latitude", json_real(lat));
    json_object_set_new(location_obj, "longitude", json_real(lon));
    json_object_set_new(data, "location", location_obj);

    /* Weather data */
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
    json_object_set_new(weather_obj, "winddirection",
                        json_integer(weather_data->winddirection));
    json_object_set_new(weather_obj, "winddirection_name",
                        json_string(open_meteo_api_get_wind_direction(
                            weather_data->winddirection)));
    json_object_set_new(weather_obj, "humidity",
                        json_real(weather_data->humidity));
    json_object_set_new(weather_obj, "pressure",
                        json_real(weather_data->pressure));
    json_object_set_new(weather_obj, "precipitation",
                        json_real(weather_data->precipitation));
    json_object_set_new(weather_obj, "is_day",
                        json_boolean(weather_data->is_day));

    json_object_set_new(data, "current_weather", weather_obj);

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

/* Cleanup weather server module */
void open_meteo_handler_cleanup(void) { open_meteo_api_cleanup(); }
