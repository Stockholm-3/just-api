/**
 * @file open_meteo_api.h
 * @brief Open-Meteo API integration for the just-weather server.
 *
 * This module provides a synchronous interface for fetching current
 * weather data from the Open-Meteo API, with optional filesystem caching.
 *
 * Features:
 *  - Fetch current weather by latitude/longitude
 *  - Local JSON caching with configurable TTL
 *  - Weather code → human-readable description mapping
 *  - Wind direction conversion (degrees → cardinal direction)
 */

#ifndef OPEN_METEO_API_H
#define OPEN_METEO_API_H

#include <stdbool.h>

/**
 * @struct WeatherData
 * @brief Holds parsed current weather information.
 *
 * This structure represents the normalized weather data returned
 * by the Open-Meteo API.
 *
 * Note:
 *  - `_raw_json_cache` is internal and must not be accessed directly.
 *  - Memory must be released using open_meteo_api_free_current().
 */
typedef struct {
  int weather_code; /**< Open-Meteo weather condition code */

  double temperature;        /**< Current temperature */
  char temperature_unit[16]; /**< Temperature unit (e.g. °C) */

  double windspeed;        /**< Wind speed */
  char windspeed_unit[16]; /**< Wind speed unit (e.g. km/h) */

  int winddirection; /**< Wind direction in degrees */

  double precipitation; /**< Precipitation amount */
  double humidity;      /**< Relative humidity (%) */
  double pressure;      /**< Surface pressure */
  int is_day;           /**< 1 if daytime, 0 if night */

  float latitude;  /**< Latitude of the weather location */
  float longitude; /**< Longitude of the weather location */

  /**
   * @internal
   * Raw JSON response from the API, used for caching.
   * This field is managed internally and freed automatically.
   */
  char *_raw_json_cache;
} WeatherData;

/**
 * @struct Location
 * @brief Geographic location descriptor.
 */
typedef struct {
  float latitude;   /**< Latitude coordinate */
  float longitude;  /**< Longitude coordinate */
  const char *name; /**< Optional human-readable name */
} Location;

/**
 * @struct WeatherConfig
 * @brief Configuration for the Open-Meteo API module.
 */
typedef struct {
  const char *cache_dir; /**< Directory used for weather cache files */
  int cache_ttl;         /**< Cache time-to-live in seconds */
  bool use_cache;        /**< Enable or disable caching */
} WeatherConfig;

/**
 * @brief Initialize the Open-Meteo API module.
 *
 * Creates cache directories (if enabled) and stores configuration.
 *
 * @param config Pointer to configuration structure
 * @return 0 on success, negative value on failure
 */
int open_meteo_api_init(WeatherConfig *config);

/**
 * @brief Fetch current weather data for a location.
 *
 * If caching is enabled and valid data exists, cached data is used.
 * Otherwise, a live API request is performed.
 *
 * @param location Pointer to location information
 * @param data Output pointer receiving allocated WeatherData
 * @return 0 on success, negative value on failure
 *
 * @note The returned WeatherData must be freed with
 *       open_meteo_api_free_current().
 */
int open_meteo_api_get_current(Location *location, WeatherData **data);

/**
 * @brief Free weather data returned by the API.
 *
 * Safely releases all internal memory.
 *
 * @param data WeatherData instance to free
 */
void open_meteo_api_free_current(WeatherData *data);

/**
 * @brief Cleanup the Open-Meteo API module.
 *
 * Currently provided for symmetry and future expansion.
 */
void open_meteo_api_cleanup(void);

/**
 * @brief Convert a weather code to a human-readable description.
 *
 * @param weather_code Open-Meteo weather code
 * @return Description string (never NULL)
 */
const char *open_meteo_api_get_description(int weather_code);

/**
 * @brief Convert wind direction degrees to a cardinal direction.
 *
 * Example outputs: "North", "South-Southeast", "West-Northwest"
 *
 * @param degrees Wind direction in degrees
 * @return Cardinal direction string
 */
const char *open_meteo_api_get_wind_direction(int degrees);

/**
 * @brief Parse latitude and longitude from a query string.
 *
 * Supported formats:
 *  - lat=X&lon=Y
 *  - lat=X&long=Y
 *
 * @param query Query string
 * @param lat Output latitude
 * @param lon Output longitude
 * @return 0 on success, negative value on failure
 */
int open_meteo_api_parse_query(const char *query, float *lat, float *lon);

#endif /* OPEN_METEO_API_H */
