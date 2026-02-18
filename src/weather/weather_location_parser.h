
#ifndef WEATHER_LOCATION_PARSER_H
#define WEATHER_LOCATION_PARSER_H

#include <stdbool.h>

#ifdef _WIN32
#    define STRCASECMP _stricmp
#else
#    include <strings.h> // for strcasecmp on Linux/macOS
#    define STRCASECMP strcasecmp
#endif

/**
 * @brief Struct to hold latitude and longitude coordinates of a location.
 */
typedef struct {
    double lat;   /**< Latitude in decimal degrees */
    double lon;   /**< Longitude in decimal degrees */
    bool   found; /**< True if the city was found in the file */
} Coordinates;

/**
 * @brief Look up the latitude and longitude of a city in a CSV file.
 *
 * The CSV file should have the following format:
 * @code
 * name,latitude,longitude
 * CityName1,lat1,lon1
 * CityName2,lat2,lon2
 * ...
 * @endcode
 *
 * The search is case-insensitive and returns the first matching city.
 *
 * @param filepath Path to the CSV file containing city data.
 * @param city_name Name of the city to search for.
 * @return Coordinates struct containing latitude, longitude, and a found flag.
 *
 * @note If the city is not found or the file cannot be opened, the `found`
 * field will be set to false and lat/lon will be 0.0.
 */
Coordinates get_city_coordinates(const char* filepath, const char* city_name);

#endif // WEATHER_LOCATION_PARSER_H
