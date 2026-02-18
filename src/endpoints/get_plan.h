#include "logger/logger.h"
#include "sys/file.h"
#include "sys/stat.h"
#include "weather_location_parser.h"

#include <http_utils.h>
#include <libgen.h> // for dirname
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include <url_query_parser.h>

#define MAX_REGISTERED_CITIES 200
#define REGISTERED_CITIES_FILE "energy_plan/cities.csv"
#define CITY_TTL_SECONDS (2ULL * 24 * 3600) // 2 days

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

/* Create all directories in the file path */
static int ensure_path_for_file(const char* filepath) {
    if (!filepath) {
        return -1;
    }

    char path[512];
    strncpy(path, filepath, sizeof(path));
    path[sizeof(path) - 1] = '\0';

    char*       dir = dirname(path);
    struct stat st  = {0};
    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0755) != 0) {
            LOG_WARN("PLAN", "Failed to create directory: %s", dir);
            return -1;
        }
    }
    return 0;
}

typedef enum { CITY_ADDED, CITY_EXISTS, CITY_LIMIT_REACHED } CityRegisterStatus;

static CityRegisterStatus register_city(const char* filepath, const char* city,
                                        const char* price, double lat,
                                        double lon) {
    if (!filepath || !city || !price) {
        return CITY_LIMIT_REACHED;
    }

    if (ensure_path_for_file(filepath) != 0) {
        return CITY_LIMIT_REACHED;
    }

    FILE* fp = fopen(filepath, "r+");
    if (!fp) {
        fp = fopen(filepath, "w+"); // create if missing
    }
    if (!fp) {
        LOG_WARN("PLAN", "Failed to open CSV file: %s", filepath);
        return CITY_LIMIT_REACHED;
    }

    if (flock(fileno(fp), LOCK_EX) != 0) {
        LOG_WARN("PLAN", "Failed to lock CSV file");
        fclose(fp);
        return CITY_LIMIT_REACHED;
    }

    typedef struct {
        char   city[256];
        char   price[16];
        double lat, lon;
        time_t last_accessed;
    } CityEntry;

    CityEntry cities[MAX_REGISTERED_CITIES];
    int       count = 0, exists_flag = 0;
    time_t    now = time(NULL);

    char line[512];
    while (fgets(line, sizeof(line), fp) && count < MAX_REGISTERED_CITIES) {
        char* token;
        char* saveptr = NULL;

        token = strtok_r(line, ",", &saveptr);
        if (!token) {
            continue;
        }
        strncpy(cities[count].city, token, sizeof(cities[count].city));
        cities[count].city[sizeof(cities[count].city) - 1] = '\0';

        token = strtok_r(NULL, ",", &saveptr);
        strncpy(cities[count].price, token ? token : "",
                sizeof(cities[count].price));
        cities[count].price[sizeof(cities[count].price) - 1] = '\0';

        token             = strtok_r(NULL, ",", &saveptr);
        cities[count].lat = token ? atof(token) : 0.0;

        token             = strtok_r(NULL, ",", &saveptr);
        cities[count].lon = token ? atof(token) : 0.0;

        token                       = strtok_r(NULL, ",", &saveptr);
        cities[count].last_accessed = token ? (time_t)atoll(token) : now;

        if (strcmp(cities[count].city, city) == 0) {
            exists_flag                 = 1;
            cities[count].last_accessed = now;
        }

        if (now - cities[count].last_accessed > CITY_TTL_SECONDS) {
            cities[count].city[0] = '\0';
        }

        count++;
    }

    CityRegisterStatus status = exists_flag ? CITY_EXISTS : CITY_ADDED;

    if (!exists_flag) {
        int valid_count = 0;
        for (int i = 0; i < count; i++) {
            if (cities[i].city[0] != '\0') {
                valid_count++;
            }
        }

        if (valid_count >= MAX_REGISTERED_CITIES) {
            status = CITY_LIMIT_REACHED;
        } else {
            strncpy(cities[count].city, city, sizeof(cities[count].city));
            strncpy(cities[count].price, price, sizeof(cities[count].price));
            cities[count].lat           = lat;
            cities[count].lon           = lon;
            cities[count].last_accessed = now;
            count++;
        }
    }

    // Safely reset the file for writing
    if (fseek(fp, 0, SEEK_SET) != 0 || ftruncate(fileno(fp), 0) != 0) {
        LOG_WARN("PLAN", "Failed to reset CSV file");
        flock(fileno(fp), LOCK_UN);
        fclose(fp);
        return status;
    }

    for (int i = 0; i < count; i++) {
        if (cities[i].city[0] != '\0') {
            if (fprintf(fp, "%s,%s,%.6f,%.6f,%ld\n", cities[i].city,
                        cities[i].price, cities[i].lat, cities[i].lon,
                        (long)cities[i].last_accessed) < 0) {
                LOG_WARN("PLAN", "Failed to write city: %s", cities[i].city);
                flock(fileno(fp), LOCK_UN);
                fclose(fp);
                return status;
            }
        }
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

    CityRegisterStatus status = register_city(REGISTERED_CITIES_FILE, city,
                                              price, coords.lat, coords.lon);

    const char* status_msg = (status == CITY_ADDED) ? "City has been added"
                             : (status == CITY_EXISTS)
                                 ? "City already exists (timestamp updated)"
                                 : "City not added; maximum limit reached";

    char response[1024];
    int  written = snprintf(response, sizeof(response),
                            "{ \"city\": \"%s\", \"price\": \"%s\", \"lat\": "
                             "%.6f, \"lon\": %.6f, \"status\": \"%s\" }",
                            city, price, coords.lat, coords.lon, status_msg);

    if (written < 0 || written >= (int)sizeof(response)) {
        return send_json_error(conn, 500, "Response too large");
    }

    return send_response(conn, 200, "application/json", response,
                         (size_t)written);
}
