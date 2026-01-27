/**
 * @file forecast.h
 * @brief Weather forecast endpoint handler
 */

#ifndef FORECAST_H
#define FORECAST_H

#include "open_meteo_handler.h"

#include <http_utils.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Handle forecast weather endpoint
 * @param conn HTTP connection
 * @param query Query string with city, country, and days parameters
 * @return 0 on success, -1 on error
 */
static inline int handle_forecast_weather(HTTPServerConnection* conn,
                                          const char*           query) {
    char* json_response = NULL;
    int   status_code   = 0;

    // Call forecast handler with query parameters
    open_meteo_handler_forecast(query, &json_response, &status_code);

    if (!json_response) {
        return send_json_error(
            conn, 500, "Failed to fetch forecast data from Open-Meteo API");
    }

    int ret = send_response(conn, status_code, "application/json",
                            json_response, strlen(json_response));
    free(json_response);
    return ret;
}

#endif // FORECAST_H
