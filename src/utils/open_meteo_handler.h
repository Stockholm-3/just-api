/**
 * @file open_meteo_handler.h
 * @brief HTTP endpoint handler for Open-Meteo weather API requests.
 *
 * This module provides HTTP request handling for weather data endpoints.
 * It acts as a bridge between the HTTP server and the Open-Meteo API client,
 * parsing incoming requests and formatting weather data responses.
 *
 * The handler supports the following endpoint:
 * - GET /v1/current - Returns current weather data for specified coordinates
 *
 * @note This module must be initialized before use and cleaned up on shutdown.
 *
 * @see open_meteo_api.h for the underlying API client
 * @see response_builder.h for JSON response formatting
 */

#ifndef OPEN_METEO_HANDLER_H
#define OPEN_METEO_HANDLER_H

/**
 * @brief Initialize the Open-Meteo handler module.
 *
 * Initializes the underlying Open-Meteo API client with default configuration:
 * - Cache directory: ./cache/weather_cache
 * - Cache TTL: 900 seconds (15 minutes)
 * - Caching enabled
 *
 * This function must be called before any other handler functions.
 *
 * @return 0 on success, non-zero on initialization failure.
 *
 * @note Thread-safety: Not thread-safe. Call only once at startup.
 */
int open_meteo_handler_init(void);

/**
 * @brief Handle GET /v1/current endpoint request.
 *
 * Processes a request for current weather data at specified coordinates.
 * Parses query parameters, fetches weather data from Open-Meteo API,
 * and builds a standardized JSON response.
 *
 * @param[in]  query_string  Query parameters string (e.g.,
 * "lat=37.7749&lon=-122.4194"). Must contain valid 'lat' and 'lon' parameters.
 * @param[out] response_json Pointer to receive the allocated JSON response
 * string. Caller is responsible for freeing this memory. Set to NULL on entry,
 * always set on return (even on error).
 * @param[out] status_code   Pointer to receive the HTTP status code.
 *                           Will be set to HTTP_OK (200), HTTP_BAD_REQUEST
 * (400), or HTTP_INTERNAL_ERROR (500).
 *
 * @return 0 on success, -1 on error.
 *
 * @note On error, response_json will contain a properly formatted error
 * response.
 *
 * @par Response Format (Success):
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
 * @par Example Usage:
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
 * @brief Clean up the Open-Meteo handler module.
 *
 * Releases all resources allocated by the handler, including
 * the underlying Open-Meteo API client resources and cache.
 *
 * Should be called during server shutdown.
 *
 * @note Thread-safety: Not thread-safe. Call only once at shutdown.
 * @note Safe to call even if init was not called or failed.
 */
void open_meteo_handler_cleanup(void);

#endif /* OPEN_METEO_HANDLER_H */
