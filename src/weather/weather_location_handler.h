/**
 * @file weather_location_handler.h
 * @brief Combined handler for geocoding and weather API integration.
 *
 * This module provides a high-level API for retrieving weather data by city
 * name. It serves as a wrapper over the geocoding_api and open_meteo_api
 * modules, combining their functionality into convenient endpoint handlers.
 *
 * The module supports two main endpoints:
 * - GET /v1/weather - Weather by city name (geocoding + weather lookup)
 * - GET /v1/cities - City search for autocomplete functionality
 *
 * @par Features:
 * - Lazy initialization (modules are initialized on first request)
 * - Support for city, country, and region parameters
 * - URL decoding for query parameters
 * - Integration with popular cities database for fast lookups
 *
 * @note This module handles initialization of both geocoding and weather APIs.
 *
 * @see geocoding_api.h for the geocoding client
 * @see open_meteo_api.h for the weather data client
 * @see open_meteo_handler.h for coordinate-based weather requests
 */

#ifndef WEATHER_LOCATION_HANDLER_H
#define WEATHER_LOCATION_HANDLER_H

/**
 * @brief Initialize the weather location handler.
 *
 * Performs explicit initialization of all dependent modules:
 * - Open-Meteo weather API client
 * - Geocoding API client
 * - Popular cities database (optional, non-critical)
 *
 * This function is optional as initialization also happens lazily
 * on the first request.
 *
 * @return 0 on success, non-zero on failure.
 *
 * @note Thread-safety: Not thread-safe. Call once at startup if explicit
 *       initialization is needed.
 */
int weather_location_handler_init(void);

/**
 * @brief Handle weather request by city name.
 *
 * Processes a weather request by:
 * 1. Parsing city, country, and region from query parameters
 * 2. Looking up coordinates via the geocoding API
 * 3. Fetching weather data for the found coordinates
 * 4. Building a combined JSON response with location and weather info
 *
 * @par Endpoint:
 * GET /v1/weather?city=<name>&country=<code>
 * GET /v1/weather?city=<name>&region=<region>&country=<code>
 *
 * @param[in]  query_string  URL query parameters. Required: city.
 *                           Optional: country (ISO code), region.
 * @param[out] response_json Pointer to receive allocated JSON response.
 *                           Caller must free this memory.
 * @param[out] status_code   Pointer to receive HTTP status code.
 *                           Possible values: 200, 400, 404, 500.
 *
 * @return 0 on success, -1 on error (response_json will contain error details).
 *
 * @par Response Format (Success):
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
 * @par Examples:
 * @code
 * /v1/weather?city=Kyiv&country=UA
 * /v1/weather?city=Stockholm
 * /v1/weather?city=Lviv&region=Lviv%20Oblast&country=UA
 * @endcode
 */
int weather_location_handler_by_city(const char* query_string,
                                     char** response_json, int* status_code);

/**
 * @brief Handle city search request for autocomplete.
 *
 * Searches for cities matching the provided query string using a
 * three-tier strategy for optimal performance:
 * 1. Popular Cities DB (in-memory, fastest)
 * 2. File cache (fast)
 * 3. Open-Meteo Geocoding API (slowest, uses network quota)
 *
 * @par Endpoint:
 * GET /v1/cities?query=<search>
 *
 * @param[in]  query_string  URL query parameters. Required: query (min 2
 * chars).
 * @param[out] response_json Pointer to receive allocated JSON response
 *                           containing list of matching cities.
 *                           Caller must free this memory.
 * @param[out] status_code   Pointer to receive HTTP status code.
 *
 * @return 0 on success, -1 on error.
 *
 * @par Response Format (Success):
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
 * @par Example:
 * @code
 * /v1/cities?query=Kyiv
 * @endcode
 */
int weather_location_handler_search_cities(const char* query_string,
                                           char**      response_json,
                                           int*        status_code);

/**
 * @brief Clean up the weather location handler.
 *
 * Releases all resources allocated by the handler and its dependencies:
 * - Geocoding API client
 * - Weather API handler
 * - Popular cities database
 *
 * Safe to call even if the handler was never initialized.
 *
 * @note Thread-safety: Not thread-safe. Call once during shutdown.
 */
void weather_location_handler_cleanup(void);

#endif /* WEATHER_LOCATION_HANDLER_H */