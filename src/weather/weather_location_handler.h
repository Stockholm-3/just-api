/**
 * @file weather_location_handler.h
 * @brief Kombinerad hanterare för geokodnings- och väder-API-integration.
 *
 * Denna modul tillhandahåller ett högnivå-API för att hämta väderdata via stadsnamn.
 * Den fungerar som en omslagarfunktion över geocoding_api och open_meteo_api-modulerna,
 * och kombinerar deras funktionalitet till praktiska endpoint-hanterare.
 *
 * Modulen stöder två huvud-endpoints:
 * - GET /v1/weather - Väder via stadsnamn (geokodning + vädersökning)
 * - GET /v1/cities - Stadssökning för autocomplete-funktionalitet
 *
 * @par Funktioner:
 * - Lat initiering (moduler initieras vid första förfrågan)
 * - Stöd för stad-, land- och regionparametrar
 * - URL-avkodning för frågeparametrar
 * - Integration med populära städer-databas för snabba sökningar
 *
 * @note Denna modul hanterar initiering av både geokodnings- och väder-API:er.
 *
 * @see geocoding_api.h för geokodningsklienten
 * @see open_meteo_api.h för väderdataklienten
 * @see open_meteo_handler.h för koordinatbaserade väderförfrågningar
 */

#ifndef WEATHER_LOCATION_HANDLER_H
#define WEATHER_LOCATION_HANDLER_H

#include "http_server_connection.h"

/**
 * @brief Initiera väderplatshanteraren.
 *
 * Utför explicit initiering av alla beroende moduler:
 * - Open-Meteo väder-API-klient
 * - Geokodnings-API-klient
 * - Populära städer-databas (valfri, icke-kritisk)
 *
 * Denna funktion är valfri eftersom initiering också sker lazily
 * vid den första förfrågan.
 *
 * @return 0 vid framgång, icke-noll vid fel.
 *
 * @note Trådsäkerhet: Inte trådsäker. Anropa en gång vid uppstart om explicit
 *       initiering behövs.
 */
int weather_location_handler_init(void);

/**
 * @brief Hantera väderförfrågan via stadsnamn.
 *
 * Bearbetar en väderförfrågan genom att:
 * 1. Parsa stad, land och region från frågeparametrar
 * 2. Slå upp koordinater via geokodnings-API:et
 * 3. Hämta väderdata för de hittade koordinaterna
 * 4. Bygga ett kombinerat JSON-svar med plats- och väderinformation
 *
 * @par Endpoint:
 * GET /v1/weather?city=<namn>&country=<kod>
 * GET /v1/weather?city=<namn>&region=<region>&country=<kod>
 *
 * @param[in]  query_string  URL-frågeparametrar. Krävs: city.
 *                           Valfritt: country (ISO-kod), region.
 * @param[out] response_json Pekare för att ta emot allokerat JSON-svar.
 *                           Anroparen måste frigöra detta minne.
 * @param[out] status_code   Pekare för att ta emot HTTP-statuskod.
 *                           Möjliga värden: 200, 400, 404, 500.
 *
 * @return 0 vid framgång, -1 vid fel (response_json innehåller feldetaljer).
 *
 * @par Svarsformat (Framgång):
 * @code{.json}
 * {
 *   "success": true,
 *   "data": {
 *     "location": {
 *       "name": "Kyiv",
 *       "country": "Ukraine",
 *       "country_code": "UA",
 *       "region": "Kyiv City",
 *       "latitude": 50.4501,
 *       "longitude": 30.5234,
 *       "population": 2884000,
 *       "timezone": "Europe/Kiev"
 *     },
 *     "current_weather": {
 *       "temperature": 15.5,
 *       "temperature_unit": "C",
 *       "weather_code": 3,
 *       "weather_description": "Overcast",
 *       ...
 *     }
 *   }
 * }
 * @endcode
 *
 * @par Exempel:
 * @code
 * /v1/weather?city=Kyiv&country=UA
 * /v1/weather?city=Stockholm
 * /v1/weather?city=Lviv&region=Lviv%20Oblast&country=UA
 * @endcode
 */
int weather_location_handler_by_city(const char* query_string,
                                     char** response_json, int* status_code);

/**
 * @brief Hantera väderförfrågan via stadsnamn (asynkron version).
 *
 * Asynkron version som skickar svaret direkt via anslutningen.
 *
 * @param[in] conn         HTTP-serveranslutning att skicka svar till.
 * @param[in] query_string URL-frågeparametrar.
 *
 * @return 0 vid lyckad initiering, -1 vid omedelbart fel.
 */
int weather_location_handler_by_city_async(HTTPServerConnection* conn,
                                           const char*           query_string);

/**
 * @brief Hantera stadssökningsförfrågan för autocomplete.
 *
 * Söker efter städer som matchar den angivna frågesträngen med en
 * tre-nivå-strategi för optimal prestanda:
 * 1. Populära städer-DB (i minnet, snabbast)
 * 2. Filcache (snabb)
 * 3. Open-Meteo Geocoding API (långsammast, använder nätverkskvot)
 *
 * @par Endpoint:
 * GET /v1/cities?query=<sökning>
 *
 * @param[in]  query_string  URL-frågeparametrar. Krävs: query (min 2 tecken).
 * @param[out] response_json Pekare för att ta emot allokerat JSON-svar
 *                           innehållande lista med matchande städer.
 *                           Anroparen måste frigöra detta minne.
 * @param[out] status_code   Pekare för att ta emot HTTP-statuskod.
 *
 * @return 0 vid framgång, -1 vid fel.
 *
 * @par Svarsformat (Framgång):
 * @code{.json}
 * {
 *   "success": true,
 *   "data": {
 *     "query": "Kyiv",
 *     "count": 3,
 *     "cities": [
 *       {
 *         "name": "Kyiv",
 *         "country": "Ukraine",
 *         "country_code": "UA",
 *         "region": "Kyiv City",
 *         "latitude": 50.4501,
 *         "longitude": 30.5234,
 *         "population": 2884000
 *       },
 *       ...
 *     ]
 *   }
 * }
 * @endcode
 *
 * @par Exempel:
 * @code
 * /v1/cities?query=Kyiv
 * @endcode
 */
int weather_location_handler_search_cities(const char* query_string,
                                           char**      response_json,
                                           int*        status_code);

/**
 * @brief Rensa väderplatshanteraren.
 *
 * Frigör alla resurser som allokerats av hanteraren och dess beroenden:
 * - Geokodnings-API-klient
 * - Väder-API-hanterare
 * - Populära städer-databas
 *
 * Säkert att anropa även om hanteraren aldrig initierades.
 *
 * @note Trådsäkerhet: Inte trådsäker. Anropa en gång vid avslut.
 */
void weather_location_handler_cleanup(void);

#endif /* WEATHER_LOCATION_HANDLER_H */