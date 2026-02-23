/**
 * @file open_meteo_handler.h
 * @brief HTTP-endpoint-hanterare för Open-Meteo väder-API-förfrågningar.
 *
 * Denna modul tillhandahåller HTTP-förfrågningshantering för väderdataendpoints.
 * Den fungerar som en brygga mellan HTTP-servern och Open-Meteo API-klienten,
 * parsar inkommande förfrågningar och formaterar väderdatasvar.
 *
 * Hanteraren stöder följande endpoint:
 * - GET /v1/current - Returnerar aktuell väderdata för angivna koordinater
 *
 * @note Denna modul måste initieras innan användning och rensas vid avslut.
 *
 * @see open_meteo_api.h för den underliggande API-klienten
 * @see response_builder.h för JSON-svarsformatering
 */

#ifndef OPEN_METEO_HANDLER_H
#define OPEN_METEO_HANDLER_H

#include "http_server_connection.h"

/**
 * @brief Initiera Open-Meteo-hanterarmodulen.
 *
 * Initierar den underliggande Open-Meteo API-klienten med standardkonfiguration:
 * - Cache-katalog: ./cache/weather_cache
 * - Cache-TTL: 900 sekunder (15 minuter)
 * - Cachelagring aktiverad
 *
 * Denna funktion måste anropas innan några andra hanterarfunktioner.
 *
 * @return 0 vid framgång, icke-noll vid initieringsfel.
 *
 * @note Trådsäkerhet: Inte trådsäker. Anropa endast en gång vid uppstart.
 */
int open_meteo_handler_init(void);

/**
 * @brief Hantera GET /v1/current endpoint-förfrågan.
 *
 * Bearbetar en förfrågan om aktuell väderdata för angivna koordinater.
 * Parsar frågeparametrar, hämtar väderdata från Open-Meteo API,
 * och bygger ett standardiserat JSON-svar.
 *
 * @param[in]  query_string  Frågeparametersträng (t.ex.,
 * "lat=37.7749&lon=-122.4194"). Måste innehålla giltiga 'lat'- och 'lon'-parametrar.
 * @param[out] response_json Pekare för att ta emot den allokerade JSON-svarssträngen.
 * Anroparen ansvarar för att frigöra detta minne. Sätts till NULL vid ingång,
 * alltid satt vid retur (även vid fel).
 * @param[out] status_code   Pekare för att ta emot HTTP-statuskoden.
 *                           Sätts till HTTP_OK (200), HTTP_BAD_REQUEST
 * (400), eller HTTP_INTERNAL_ERROR (500).
 *
 * @return 0 vid framgång, -1 vid fel.
 *
 * @note Vid fel kommer response_json att innehålla ett korrekt formaterat felsvar.
 *
 * @par Svarsformat (Framgång):
 * @code{.json}
 * {
 *   "success": true,
 *   "data": {
 *     "current_weather": {
 *       "temperature": 20.5,
 *       "temperature_unit": "C",
 *       "windspeed": 10.2,
 *       "windspeed_unit": "km/h",
 *       "wind_direction_10m": 180,
 *       "wind_direction_name": "S",
 *       "weather_code": 0,
 *       "weather_description": "Clear sky",
 *       "is_day": 1,
 *       "precipitation": 0.0,
 *       "precipitation_unit": "mm",
 *       "humidity": 65.0,
 *       "pressure": 1013.25,
 *       "time": "2024-01-15T14:30"
 *     },
 *     "location": {
 *       "latitude": 37.7749,
 *       "longitude": -122.4194
 *     }
 *   }
 * }
 * @endcode
 *
 * @par Exempelanvändning:
 * @code{.c}
 * char* json = NULL;
 * int status = 0;
 * int result = open_meteo_handler_current("lat=37.7749&lon=-122.4194", &json,
 * &status); if (result == 0) { send_http_response(client_fd, status,
 * "application/json", json);
 * }
 * free(json);
 * @endcode
 */
int open_meteo_handler_current(const char* query_string, char** response_json,
                               int* status_code);

/**
 * @brief Hantera GET /v1/current endpoint-förfrågan (asynkron version).
 *
 * Bearbetar en förfrågan om aktuell väderdata för angivna koordinater med
 * det asynkrona API:et. Svaret skickas direkt till anslutningen via callback.
 *
 * @param[in] conn         HTTP-serveranslutning att skicka svar till.
 * @param[in] query_string Frågeparametersträng (t.ex.,
 * "lat=37.7749&lon=-122.4194").
 *
 * @return 0 vid lyckad initiering, -1 vid omedelbart fel.
 *
 * @note Detta är den rekommenderade asynkrona versionen. Svar skickas via callback.
 */
int open_meteo_handler_current_async(HTTPServerConnection* conn,
                                     const char*           query_string);

/**
 * @brief Rensa Open-Meteo-hanterarmodulen.
 *
 * Frigör alla resurser som allokerats av hanteraren, inklusive
 * den underliggande Open-Meteo API-klientens resurser och cache.
 *
 * Ska anropas vid serveravstängning.
 *
 * @note Trådsäkerhet: Inte trådsäker. Anropa endast en gång vid avslut.
 * @note Säkert att anropa även om init inte anropades eller misslyckades.
 */
void open_meteo_handler_cleanup(void);

#endif /* OPEN_METEO_HANDLER_H */
