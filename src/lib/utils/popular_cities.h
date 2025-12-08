/**
 * popular_cities.h - Local database of popular cities for autocomplete
 *
 * Implements dual-file strategy:
 * - Hot cache: Top 100-1000 cities loaded in RAM at startup
 * - Full database: All cities lazy-loaded from disk when needed
 */

#ifndef POPULAR_CITIES_H
#define POPULAR_CITIES_H

#include <stdbool.h>
#include <stddef.h>

/* City data structure */
typedef struct {
    char name[128];
    char country[64];
    char country_code[8];
    double latitude;
    double longitude;
    int population;
} PopularCity;

/* Database structure with dual-file support */
typedef struct {
    PopularCity* hot_cities;    /* Loaded in RAM at startup */
    size_t hot_count;

    char* full_db_path;         /* Path to all_cities.json */
    PopularCity* full_cities;   /* NULL until lazy-loaded */
    size_t full_count;
    bool full_loaded;
} PopularCitiesDB;

/**
 * Load popular cities database
 *
 * @param hot_file Path to hot_cities.json (loaded immediately)
 * @param full_file Path to all_cities.json (lazy-loaded)
 * @param db Output pointer to database
 * @return 0 on success, negative on error
 */
int popular_cities_load(const char* hot_file, const char* full_file,
                       PopularCitiesDB** db);

/**
 * Search for cities by name prefix
 *
 * @param db Database instance
 * @param query Search query (case-insensitive prefix match)
 * @param results Output array of pointers to matching cities
 * @param count Output count of results
 * @param max_results Maximum number of results to return
 * @return 0 on success, negative on error
 */
int popular_cities_search(PopularCitiesDB* db, const char* query,
                         PopularCity** results, size_t* count,
                         size_t max_results);

/**
 * Free database resources
 *
 * @param db Database instance to free
 */
void popular_cities_free(PopularCitiesDB* db);

#endif /* POPULAR_CITIES_H */
