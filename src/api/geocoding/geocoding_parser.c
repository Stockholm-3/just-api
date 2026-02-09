/**
 * geocoding_parser.c - Implementation av JSON-parsning för geokodningssvar
 */

#include <geocoding_parser.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int geocoding_parse_json(const char* json_str, GeocodingResponse** response) {
    json_error_t error;
    json_t*      root = json_loadb(json_str, strlen(json_str), 0, &error);

    if (!root) {
        fprintf(stderr, "[GEOCODING] JSON parse error: %s\n", error.text);
        return -1;
    }

    json_t* results_array = json_object_get(root, "results");
    if (!results_array) {
        /* Inga resultat */
        *response            = calloc(1, sizeof(GeocodingResponse));
        (*response)->count   = 0;
        (*response)->results = NULL;
        json_decref(root);
        return 0;
    }

    if (!json_is_array(results_array)) {
        fprintf(stderr, "[GEOCODING] Ogiltigt resultatformat\n");
        json_decref(root);
        return -2;
    }

    size_t count = json_array_size(results_array);
    if (count == 0) {
        *response            = calloc(1, sizeof(GeocodingResponse));
        (*response)->count   = 0;
        (*response)->results = NULL;
        json_decref(root);
        return 0;
    }

    /* Allokera minne */
    *response = calloc(1, sizeof(GeocodingResponse));
    if (!*response) {
        json_decref(root);
        return -3;
    }

    (*response)->results = calloc(count, sizeof(GeocodingResult));
    if (!(*response)->results) {
        free(*response);
        json_decref(root);
        return -4;
    }

    (*response)->count = count;

    /* Parsar varje resultat */
    for (size_t i = 0; i < count; i++) {
        json_t*          item   = json_array_get(results_array, i);
        GeocodingResult* result = &(*response)->results[i];

        /* Obligatoriska fält */
        json_t* id           = json_object_get(item, "id");
        json_t* name         = json_object_get(item, "name");
        json_t* lat          = json_object_get(item, "latitude");
        json_t* lon          = json_object_get(item, "longitude");
        json_t* country_name = json_object_get(item, "country");
        json_t* country_code = json_object_get(item, "country_code");

        if (id) {
            result->id = json_integer_value(id);
        }
        if (name && json_is_string(name)) {
            strncpy(result->name, json_string_value(name),
                    sizeof(result->name) - 1);
        }
        if (lat) {
            result->latitude = json_real_value(lat);
        }
        if (lon) {
            result->longitude = json_real_value(lon);
        }
        if (country_name && json_is_string(country_name)) {
            strncpy(result->country, json_string_value(country_name),
                    sizeof(result->country) - 1);
        }
        if (country_code && json_is_string(country_code)) {
            strncpy(result->country_code, json_string_value(country_code),
                    sizeof(result->country_code) - 1);
        }

        /* Valfria fält */
        json_t* admin1     = json_object_get(item, "admin1");
        json_t* admin2     = json_object_get(item, "admin2");
        json_t* population = json_object_get(item, "population");
        json_t* timezone   = json_object_get(item, "timezone");

        if (admin1 && json_is_string(admin1)) {
            strncpy(result->admin1, json_string_value(admin1),
                    sizeof(result->admin1) - 1);
        }
        if (admin2 && json_is_string(admin2)) {
            strncpy(result->admin2, json_string_value(admin2),
                    sizeof(result->admin2) - 1);
        }
        if (population && json_is_integer(population)) {
            result->population = json_integer_value(population);
        }
        if (timezone && json_is_string(timezone)) {
            strncpy(result->timezone, json_string_value(timezone),
                    sizeof(result->timezone) - 1);
        }
    }

    json_decref(root);
    return 0;
}
