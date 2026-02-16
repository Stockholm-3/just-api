/**
 * @file minutely.h
 * @brief HTTP endpoint handler for 15-minute interval weather forecast.
 *
 * Provides 15-minute interval weather forecast data using Open-Meteo
 * minutely_15 API parameter.
 *
 * Endpoint: GET /v1/minutely
 * Query parameters (coordinates mode):
 *   - lat (required): Latitude
 *   - lon (required): Longitude
 *   - hours (optional): Number of hours to forecast (default: 24, max: 168)
 *
 * Query parameters (city mode):
 *   - city (required): City name
 *   - country (optional): Country code (ISO)
 *   - region (optional): Region name
 *   - hours (optional): Number of hours to forecast (default: 24, max: 168)
 */

#ifndef ENDPOINTS_MINUTELY_H
#define ENDPOINTS_MINUTELY_H

#include "open_meteo_handler.h"
#include "weather_location_handler.h"

#include <http_utils.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Handle GET /v1/minutely endpoint request.
 *
 * Supports two modes:
 * - By coordinates: /v1/minutely?lat=XX&lon=YY&hours=24
 * - By city name: /v1/minutely?city=NAME&country=CODE&hours=24
 *
 * @param conn  HTTP connection object
 * @param query Query string parameters
 * @return 0 on success, non-zero on error
 */
int handle_minutely_weather(HTTPServerConnection* conn, const char* query) {
    /* Check if city parameter is present */
    if (strstr(query, "city=") != NULL) {
        /* City mode - use geocoding + minutely weather (async) */
        return weather_location_handler_minutely_by_city_async(conn, query);
    } else {
        /* Coordinates mode - use direct minutely weather (async) */
        return open_meteo_handler_minutely_async(conn, query);
    }
}

#endif /* ENDPOINTS_MINUTELY_H */
