#include "weather_location_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Look up the latitude and longitude of a city in a CSV file.
 *
 * See the header for detailed documentation.
 */
Coordinates get_city_coordinates(const char* filepath, const char* city_name) {
    Coordinates result = {0.0, 0.0, false};
    FILE*       file   = fopen(filepath, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file %s\n", filepath);
        return result;
    }

    char line[512];

    // Skip the header line
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return result;
    }

    while (fgets(line, sizeof(line), file)) {
        // Remove newline characters
        line[strcspn(line, "\r\n")] = 0;

        // Split line by commas
        char* name    = strtok(line, ",");
        char* lat_str = strtok(NULL, ",");
        char* lon_str = strtok(NULL, ",");

        if (!name || !lat_str || !lon_str) {
            continue;
        }

        // Case-insensitive comparison
        if (STRCASECMP(name, city_name) == 0) {
            result.lat   = atof(lat_str);
            result.lon   = atof(lon_str);
            result.found = true;
            break;
        }
    }

    fclose(file);
    return result;
}
