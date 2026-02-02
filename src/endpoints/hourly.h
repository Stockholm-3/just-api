/**
 * @file hourly.h
 * @brief HTTP endpoint handler for hourly weather forecast.
 *
 * Provides hourly weather forecast data.
 *
 * Endpoint: GET /v1/hourly
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

#ifndef ENDPOINTS_HOURLY_H
#define ENDPOINTS_HOURLY_H

#include "open_meteo_handler.h"
#include "weather_location_handler.h"

#include <http_utils.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Handle GET /v1/hourly endpoint request.
 *
 * Supports two modes:
 * - By coordinates: /v1/hourly?lat=XX&lon=YY&hours=24
 * - By city name: /v1/hourly?city=NAME&country=CODE&hours=24
 *
 * @param conn  HTTP connection object
 * @param query Query string parameters
 * @return 0 on success, non-zero on error
 */
int handle_hourly_weather(HTTPServerConnection* conn, const char* query) {
    /* Check if city parameter is present */
    if (strstr(query, "city=") != NULL) {
        /* City mode - use geocoding + hourly weather (async) */
        return weather_location_handler_hourly_by_city_async(conn, query);
    } else {
        /* Coordinates mode - use direct hourly weather (async) */
        return open_meteo_handler_hourly_async(conn, query);
    }
}

#endif /* ENDPOINTS_HOURLY_H */
