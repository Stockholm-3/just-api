#include "logger/logger.h"
#include "sys/file.h"
#include "weather_location_parser.h"

#include <file_cache.h>
#include <http_utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <url_query_parser.h>

#define MAX_REGISTERED_CITIES 200
#define REGISTERED_CITIES_FILE "cities.csv"

static const char* ALLOWED_PRICE_VALUES[] = {"SE1", "SE2", "SE3", "SE4"};
#define NUM_ALLOWED_PRICES                                                     \
    (sizeof(ALLOWED_PRICE_VALUES) / sizeof(ALLOWED_PRICE_VALUES[0]))

static int is_allowed_price(const char* value) {
    if (!value) {
        return 0;
    }
    for (size_t i = 0; i < NUM_ALLOWED_PRICES; i++) {
        if (strcmp(value, ALLOWED_PRICE_VALUES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static FileCacheInstance* get_plan_cache(void) {
    static FileCacheInstance* cache = NULL;
    if (!cache) {
        FileCacheConfig config = {.cache_dir = "./cache/registered_cities",
                                  .ttl_seconds =
                                      3600, /* not really used for CSV */
                                  .enabled = true};
        cache                  = file_cache_create(&config);
    }
    return cache;
}

typedef enum { CITY_ADDED, CITY_EXISTS, CITY_LIMIT_REACHED } CityRegisterStatus;

static CityRegisterStatus register_city(FileCacheInstance* cache,
                                        const char* city, const char* price,
                                        double lat, double lon) {
    if (!cache || !city) {
        return CITY_LIMIT_REACHED;
    }

    char filepath[FILE_CACHE_MAX_PATH_LENGTH];
    if (file_cache_get_filepath(cache, REGISTERED_CITIES_FILE, filepath,
                                sizeof(filepath)) != FILE_CACHE_OK) {
        LOG_WARN("PLAN", "Failed to get CSV filepath");
        return CITY_LIMIT_REACHED;
    }

    FILE* fp = fopen(filepath, "r+"); /* open for reading/writing */
    if (!fp) {
        /* File doesn't exist yet, create */
        fp = fopen(filepath, "w+");
        if (!fp) {
            LOG_WARN("PLAN", "Failed to create CSV file");
            return CITY_LIMIT_REACHED;
        }
    }

    /* Acquire exclusive lock */
    if (flock(fileno(fp), LOCK_EX) != 0) {
        LOG_WARN("PLAN", "Failed to lock CSV file");
        fclose(fp);
        return CITY_LIMIT_REACHED;
    }

    /* Read existing cities */
    char line[512];
    char cities[MAX_REGISTERED_CITIES][256];
    int  count  = 0;
    int  exists = 0;

    rewind(fp);

    while (fgets(line, sizeof(line), fp) && count < MAX_REGISTERED_CITIES) {
        char* token;
        char* saveptr = NULL;

        token = strtok_r(line, ",", &saveptr);
        if (token) {
            strncpy(cities[count], token, sizeof(cities[count]));
            cities[count][sizeof(cities[count]) - 1] = '\0';

            if (strcmp(cities[count], city) == 0) {
                exists = 1;
            }

            count++;
        }
    }

    CityRegisterStatus status = CITY_ADDED;

    if (exists) {
        status = CITY_EXISTS;
    } else if (count >= MAX_REGISTERED_CITIES) {
        status = CITY_LIMIT_REACHED;
    } else {
        /* Append new city */
        fseek(fp, 0, SEEK_END);
        fprintf(fp, "%s,%s,%.6f,%.6f\n", city, price, lat, lon);
    }

    flock(fileno(fp), LOCK_UN);
    fclose(fp);

    return status;
}

int handle_get_plan(HTTPServerConnection* conn, const char* query) {
    if (!query || *query == '\0') {
        return send_json_error(conn, 400, "Missing query parameters");
    }

    UrlQueryMap map;
    if (url_query_parse(query, &map) != 0) {
        return send_json_error(conn, 400, "Invalid query parameters");
    }

    const char* city  = url_query_get(&map, "city");
    const char* price = url_query_get(&map, "price");

    if (!city || !price) {
        return send_json_error(conn, 400,
                               "Missing required parameters: city and price");
    }

    if (!is_allowed_price(price)) {
        return send_json_error(
            conn, 400,
            "Invalid price parameter; must be SE1, SE2, SE3, or SE4");
    }

    Coordinates coords =
        get_city_coordinates("data/swedish_cities_locations.csv", city);

    if (!coords.found) {
        char err[256];
        snprintf(err, sizeof(err), "City not found: %s", city);
        return send_json_error(conn, 400, err);
    }

    FileCacheInstance* cache = get_plan_cache();
    CityRegisterStatus status =
        register_city(cache, city, price, coords.lat, coords.lon);

    char response[1024];

    const char* status_msg = NULL;
    switch (status) {
    case CITY_ADDED:
        status_msg = "City has been added";
        break;
    case CITY_EXISTS:
        status_msg = "City already exists";
        break;
    case CITY_LIMIT_REACHED:
        status_msg = "City not added; maximum limit reached";
        break;
    }

    int written = snprintf(response, sizeof(response),
                           "{"
                           " \"city\": \"%s\","
                           " \"price\": \"%s\","
                           " \"lat\": %.6f,"
                           " \"lon\": %.6f,"
                           " \"status\": \"%s\""
                           " }",
                           city, price, coords.lat, coords.lon, status_msg);

    if (written < 0 || written >= (int)sizeof(response)) {
        return send_json_error(conn, 500, "Response too large");
    }

    return send_response(conn, 200, "application/json", response,
                         (size_t)written);
}
