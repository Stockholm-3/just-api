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
    // TODO: Implement actual forecast handler
    // For now, delegate to Open-Meteo current weather handler as a placeholder
    return open_meteo_handler_current_async(conn, query);
}

#endif // FORECAST_H
