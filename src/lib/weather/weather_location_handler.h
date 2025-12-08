/**
 * weather_location_handler.h - Combined handler for geocoding + weather API
 *
 * This module is a wrapper over geocoding_api and open_meteo_api
 * Provides a single endpoint to get weather by city name
 */

#ifndef WEATHER_LOCATION_HANDLER_H
#define WEATHER_LOCATION_HANDLER_H

/**
 * Initialize the weather location handler
 * Calls initialization for both modules (geocoding + weather)
 *
 * @return 0 on success
 */
int weather_location_handler_init(void);

/**
 * Handle weather request by city name
 *
 * Endpoint: GET /v1/weather?city=<name>&country=<code>
 * or:      GET /v1/weather?city=<name>&region=<region>&country=<code>
 *
 * Process:
 * 1. Parse request parameters (city, country, region)
 * 2. Call geocoding API to obtain coordinates
 * 3. Call weather API with the obtained coordinates
 * 4. Return weather together with city information
 *
 * @param query_string Query parameters
 * @param response_json Output JSON (caller must free)
 * @param status_code HTTP status code
 * @return 0 on success
 *
 * Examples:
 *   /v1/weather?city=Kyiv&country=UA
 *   /v1/weather?city=Stockholm
 *   /v1/weather?city=Lviv&region=Lviv%20Oblast&country=UA
 */
int weather_location_handler_by_city(const char* query_string,
                                     char** response_json, int* status_code);

/**
 * Handle city list request (for autocomplete)
 *
 * Endpoint: GET /v1/cities?query=<search>
 *
 * @param query_string Query parameters
 * @param response_json Output JSON list of cities
 * @param status_code HTTP status code
 * @return 0 on success
 *
 * Example:
 *   /v1/cities?query=Kyiv
 */
int weather_location_handler_search_cities(const char* query_string,
                                           char**      response_json,
                                           int*        status_code);

/**
 * Cleanup the handler module
 */
void weather_location_handler_cleanup(void);

#endif /* WEATHER_LOCATION_HANDLER_H */