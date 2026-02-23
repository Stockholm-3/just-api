/**
 * @file forecast.h
 * @brief Endpoint-hanterare för väderprognos
 */

#ifndef FORECAST_H
#define FORECAST_H

#include "open_meteo_handler.h"

#include <http_utils.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Hantera endpoint för väderprognos
 * @param conn HTTP-anslutning
 * @param query Frågesträng med stad-, land- och dagarparametrar
 * @return 0 vid framgång, -1 vid fel
 */
static inline int handle_forecast_weather(HTTPServerConnection* conn,
                                          const char*           query) {
    // TODO: Implementera riktig prognoshanterare
    // For nu, delegera till Open-Meteo aktuellt vaderhanterare som platshallare
    return open_meteo_handler_current_async(conn, query);
}

#endif // FORECAST_H
