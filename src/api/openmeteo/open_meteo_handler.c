/**
 * @file open_meteo_handler.c
 * @brief Implementering av HTTP-endpoint-hanterare för Open-Meteo väder-API.
 *
 * Denna fil implementerar HTTP-förfrågningshanteringslogiken för väderendpoints.
 * Den bearbetar inkommande förfrågningar, interagerar med Open-Meteo API-klienten,
 * och formaterar svar med den standardiserade svarsbyggaren.
 *
 * @see open_meteo_handler.h för det publika gränssnittet
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

/* ============= Interna strukturer ============= */

typedef struct {
    HTTPServerConnection* conn;
    float                 lat;
    float                 lon;
} AsyncHandlerContext;

/* ============= Interna funktionsdeklarationer ============= */

static void weather_fetch_callback(int status, WeatherData* data,
                                   void* context);

/**
 * @brief Initiera Open-Meteo-hanterarmodulen.
 *
 * Konfigurerar och initierar Open-Meteo API-klienten med cachelagring aktiverad.
 *
 * @return 0 vid framgång, icke-noll vid fel.
 */
int open_meteo_handler_init(void) {
    WeatherConfig config = {.cache_dir = "./cache/weather_cache",
                            .cache_ttl = 900, /* 15 minuter */
                            .use_cache = true};

    return open_meteo_api_init(&config);
}

/**
 * @brief Hantera GET /v1/current endpoint-förfrågan.
 *
 * Parsar latitud och longitud från frågesträng, hämtar aktuell väderdata
 * från Open-Meteo API, och bygger ett JSON-svar med väderdetaljer.
 *
 * @param[in]  query_string  URL-frågeparametrar som innehåller lat och lon.
 * @param[out] response_json Allokerad JSON-svarssträng (anroparen frigör).
 * @param[out] status_code   HTTP-statuskod för svaret.
 *
 * @return 0 vid framgång, -1 vid fel.
 */
int open_meteo_handler_current(const char* query_string, char** response_json,
                               int* status_code) {
    if (!response_json || !status_code) {
        return -1;
    }

    *response_json = NULL;
    *status_code   = HTTP_INTERNAL_ERROR;

    /* Parsa frågeparametrar */
    float lat, lon;
    if (open_meteo_api_parse_query(query_string, &lat, &lon) != 0) {
        *response_json = response_builder_error(
            HTTP_BAD_REQUEST, response_builder_get_error_type(HTTP_BAD_REQUEST),
            "Invalid query parameters. Expected format: "
            "lat=XX.XXXX&lon=YY.YYYY");
        *status_code = HTTP_BAD_REQUEST;
        return -1;
    }

    /* Skapa plats */
    Location location = {
        .latitude = lat, .longitude = lon, .name = "Query Location"};

    /* Hämta aktuellt väder */
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

    /* Bygg strukturerat JSON-svar */
    json_t* data = json_object();

    /* Väderdata - lägg till först (ordningen matchar dokumentationen) */
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

    /* Formatera tid som "YYYY-MM-DDTHH:MM" */
    time_t     now     = time(NULL);
    struct tm* tm_info = localtime(&now);
    char       time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M", tm_info);
    json_object_set_new(weather_obj, "time", json_string(time_str));

    json_object_set_new(data, "current_weather", weather_obj);

    /* Platsinformation - lägg till sist */
    json_t* location_obj = json_object();
    json_object_set_new(location_obj, "latitude", json_real(lat));
    json_object_set_new(location_obj, "longitude", json_real(lon));
    json_object_set_new(data, "location", location_obj);

    /* Rensa väderdata */
    open_meteo_api_free_current(weather_data);

    /* Bygg standardiserat svar */
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
 * @brief Rensa Open-Meteo-hanterarmodulen.
 *
 * Frigör alla resurser som allokerats av den underliggande Open-Meteo API-klienten.
 */
void open_meteo_handler_cleanup(void) { open_meteo_api_cleanup(); }

/* ============= Implementering av asynkron hanterare ============= */

/**
 * @brief Callback för asynkron hämtning av väderdata.
 *
 * Anropas när väderdata har hämtats. Bygger och skickar HTTP-svaret.
 */
static void weather_fetch_callback(int status, WeatherData* data,
                                   void* context) {
    AsyncHandlerContext* ctx = (AsyncHandlerContext*)context;
    if (!ctx || !ctx->conn) {
        free(ctx);
        return;
    }

    if (status != 0 || !data) {
        /* Fel vid hämtning av väderdata */
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

    /* Bygg strukturerat JSON-svar */
    json_t* response_data = json_object();

    /* Väderdata */
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

    /* Formatera tid */
    time_t     now     = time(NULL);
    struct tm* tm_info = localtime(&now);
    char       time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M", tm_info);
    json_object_set_new(weather_obj, "time", json_string(time_str));

    json_object_set_new(response_data, "current_weather", weather_obj);

    /* Platsinformation */
    json_t* location_obj = json_object();
    json_object_set_new(location_obj, "latitude", json_real(ctx->lat));
    json_object_set_new(location_obj, "longitude", json_real(ctx->lon));
    json_object_set_new(response_data, "location", location_obj);

    /* Bygg standardiserat svar */
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

    /* Rensa */
    open_meteo_api_free_current(data);
    free(ctx);
}

/**
 * @brief Hantera GET /v1/current endpoint-förfrågan (asynkron version).
 */
int open_meteo_handler_current_async(HTTPServerConnection* conn,
                                     const char*           query_string) {
    if (!conn) {
        return -1;
    }

    /* Parsa frågeparametrar */
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

    /* Skapa kontext för callback */
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

    /* Skapa plats */
    Location location = {
        .latitude = lat, .longitude = lon, .name = "Query Location"};

    /* Hämta aktuellt väder asynkront */
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
