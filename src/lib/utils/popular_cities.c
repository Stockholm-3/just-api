/**
 * popular_cities.c - Implementation of popular cities database
 */

#include "popular_cities.h"

#include <ctype.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ============= Internal Functions ============= */

static int load_cities_from_json(const char* filepath, PopularCity** cities,
                                 size_t* count);
static void normalize_query(const char* input, char* output, size_t output_size);

/* ============= Public API Implementation ============= */

int popular_cities_load(const char* hot_file, const char* full_file,
                       PopularCitiesDB** db) {
    if (!hot_file || !full_file || !db) {
        fprintf(stderr, "[POPULAR_CITIES] Invalid parameters\n");
        return -1;
    }

    /* Allocate database structure */
    PopularCitiesDB* database = (PopularCitiesDB*)calloc(1, sizeof(PopularCitiesDB));
    if (!database) {
        fprintf(stderr, "[POPULAR_CITIES] Failed to allocate database\n");
        return -2;
    }

    /* Load hot cities (immediately) */
    printf("[POPULAR_CITIES] Loading hot cities from: %s\n", hot_file);

    int result = load_cities_from_json(hot_file, &database->hot_cities,
                                      &database->hot_count);

    if (result != 0) {
        fprintf(stderr, "[POPULAR_CITIES] Failed to load hot cities\n");
        free(database);
        return -3;
    }

    printf("[POPULAR_CITIES] Loaded %zu hot cities\n", database->hot_count);

    /* Save full database path for lazy loading */
    database->full_db_path = strdup(full_file);
    database->full_cities = NULL;
    database->full_count = 0;
    database->full_loaded = false;

    *db = database;
    return 0;
}

int popular_cities_search(PopularCitiesDB* db, const char* query,
                         PopularCity** results, size_t* count,
                         size_t max_results) {
    if (!db || !query || !results || !count) {
        return -1;
    }

    *count = 0;

    /* Normalize query for case-insensitive comparison */
    char normalized_query[256];
    normalize_query(query, normalized_query, sizeof(normalized_query));

    size_t query_len = strlen(normalized_query);

    if (query_len < 2) {
        return -1; /* Query too short */
    }

    /* Search in hot cities first */
    for (size_t i = 0; i < db->hot_count && *count < max_results; i++) {
        char normalized_city[128];
        normalize_query(db->hot_cities[i].name, normalized_city,
                       sizeof(normalized_city));

        /* Prefix match */
        if (strncmp(normalized_city, normalized_query, query_len) == 0) {
            results[*count] = &db->hot_cities[i];
            (*count)++;
        }
    }

    /* If we found enough results in hot cities, return */
    if (*count > 0) {
        return 0;
    }

    /* Lazy load full database if needed */
    if (!db->full_loaded && db->full_db_path) {
        printf("[POPULAR_CITIES] Lazy loading full database from: %s\n",
               db->full_db_path);

        int result = load_cities_from_json(db->full_db_path, &db->full_cities,
                                          &db->full_count);

        if (result == 0) {
            db->full_loaded = true;
            printf("[POPULAR_CITIES] Loaded %zu cities from full database\n",
                   db->full_count);
        } else {
            fprintf(stderr, "[POPULAR_CITIES] Failed to lazy load full database\n");
            return 0; /* Return empty results instead of error */
        }
    }

    /* Search in full database */
    if (db->full_loaded && db->full_cities) {
        for (size_t i = 0; i < db->full_count && *count < max_results; i++) {
            char normalized_city[128];
            normalize_query(db->full_cities[i].name, normalized_city,
                           sizeof(normalized_city));

            /* Prefix match */
            if (strncmp(normalized_city, normalized_query, query_len) == 0) {
                results[*count] = &db->full_cities[i];
                (*count)++;
            }
        }
    }

    return 0;
}

void popular_cities_free(PopularCitiesDB* db) {
    if (!db) {
        return;
    }

    /* Free hot cities */
    if (db->hot_cities) {
        free(db->hot_cities);
        db->hot_cities = NULL;
    }

    /* Free full cities */
    if (db->full_cities) {
        free(db->full_cities);
        db->full_cities = NULL;
    }

    /* Free full database path */
    if (db->full_db_path) {
        free(db->full_db_path);
        db->full_db_path = NULL;
    }

    /* Free database structure */
    free(db);

    printf("[POPULAR_CITIES] Database freed\n");
}

/* ============= Internal Functions Implementation ============= */

static int load_cities_from_json(const char* filepath, PopularCity** cities,
                                 size_t* count) {
    if (!filepath || !cities || !count) {
        return -1;
    }

    /* Load JSON file */
    json_error_t error;
    json_t* root = json_load_file(filepath, 0, &error);

    if (!root) {
        fprintf(stderr, "[POPULAR_CITIES] JSON load error: %s\n", error.text);
        return -2;
    }

    /* Get cities array */
    json_t* cities_array = json_object_get(root, "cities");

    if (!cities_array || !json_is_array(cities_array)) {
        fprintf(stderr, "[POPULAR_CITIES] Invalid JSON format: missing 'cities' array\n");
        json_decref(root);
        return -3;
    }

    size_t num_cities = json_array_size(cities_array);

    if (num_cities == 0) {
        fprintf(stderr, "[POPULAR_CITIES] Empty cities array\n");
        json_decref(root);
        return -4;
    }

    /* Allocate cities array */
    PopularCity* city_list = (PopularCity*)calloc(num_cities, sizeof(PopularCity));

    if (!city_list) {
        fprintf(stderr, "[POPULAR_CITIES] Failed to allocate cities array\n");
        json_decref(root);
        return -5;
    }

    /* Parse each city */
    for (size_t i = 0; i < num_cities; i++) {
        json_t* city_obj = json_array_get(cities_array, i);

        if (!json_is_object(city_obj)) {
            continue;
        }

        PopularCity* city = &city_list[i];

        /* Parse name */
        json_t* name = json_object_get(city_obj, "name");
        if (name && json_is_string(name)) {
            strncpy(city->name, json_string_value(name), sizeof(city->name) - 1);
        }

        /* Parse country */
        json_t* country = json_object_get(city_obj, "country");
        if (country && json_is_string(country)) {
            strncpy(city->country, json_string_value(country),
                   sizeof(city->country) - 1);
        }

        /* Parse country_code */
        json_t* country_code = json_object_get(city_obj, "country_code");
        if (country_code && json_is_string(country_code)) {
            strncpy(city->country_code, json_string_value(country_code),
                   sizeof(city->country_code) - 1);
        }

        /* Parse latitude */
        json_t* lat = json_object_get(city_obj, "lat");
        if (lat && json_is_number(lat)) {
            city->latitude = json_number_value(lat);
        }

        /* Parse longitude */
        json_t* lon = json_object_get(city_obj, "lon");
        if (lon && json_is_number(lon)) {
            city->longitude = json_number_value(lon);
        }

        /* Parse population */
        json_t* population = json_object_get(city_obj, "population");
        if (population && json_is_integer(population)) {
            city->population = json_integer_value(population);
        }
    }

    json_decref(root);

    *cities = city_list;
    *count = num_cities;

    return 0;
}

static void normalize_query(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return;
    }

    size_t j = 0;

    for (size_t i = 0; input[i] != '\0' && j + 1 < output_size; i++) {
        unsigned char c = (unsigned char)input[i];

        /* Convert to lowercase (ASCII only) */
        if (c >= 'A' && c <= 'Z') {
            output[j++] = (char)(c - 'A' + 'a');
        } else if (c >= 'a' && c <= 'z') {
            output[j++] = (char)c;
        } else if (c >= '0' && c <= '9') {
            output[j++] = (char)c;
        } else if (c == ' ' || c == '-' || c == '_') {
            /* Keep spaces and dashes */
            output[j++] = (char)c;
        }
        /* Skip other characters */
    }

    output[j] = '\0';
}
