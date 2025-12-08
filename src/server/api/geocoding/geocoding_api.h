/**
 * geocoding_api.h - Geocoding API for searching city coordinates
 *
 * Module for converting a city name into geographic coordinates.
 * Uses the Open-Meteo Geocoding API with result caching.
 */

#ifndef GEOCODING_API_H
#define GEOCODING_API_H

#include <stdbool.h>
#include <stddef.h>

#define GEOCODING_MAX_RESULTS 10

/* Maximum number of search results */
#define GEOCODING_MAX_RESULTS 10

/* Structure with city information */
typedef struct {
    float latitude;
    float longitude;
    char  name[128];       /* City name */
    char  country[64];     /* Country */
    char  country_code[8]; /* Country code (UA, US, etc.) */
    char  admin1[64];      /* Region / province / state */
    char  admin2[64];      /* District */
    int   population;      /* Population */
    char  timezone[64];    /* Time zone */
    int   id;              /* Place ID */
} GeocodingResult;

/* Structure with search results */
typedef struct {
    GeocodingResult* results; /* Array of results */
    int              count;   /* Number of found results */
} GeocodingResponse;

/* Configuration for the geocoding module */
typedef struct {
    const char* cache_dir;   /* Directory for cache files */
    int         cache_ttl;   /* Cache TTL in seconds (default: 7 days) */
    bool        use_cache;   /* Use cache */
    int         max_results; /* Maximum number of results */
    const char* language;    /* Result language (uk, en, ru, etc.) */
} GeocodingConfig;

/**
 * Initialize the geocoding API
 *
 * @param config Configuration (NULL for defaults)
 * @return 0 on success, -1 on error
 */
int geocoding_api_init(GeocodingConfig* config);

/**
 * Search for a city by name
 *
 * @param city_name City name to search for (required)
 * @param country Country code to filter results (optional, may be NULL)
 * @param response Pointer to the search result (caller must free)
 * @return 0 on success, < 0 on error
 *
 * Usage examples:
 *   geocoding_api_search("Kyiv", "UA", &response);
 *   geocoding_api_search("Stockholm", NULL, &response);
 *   geocoding_api_search("London", "GB", &response);
 */
int geocoding_api_search(const char* city_name, const char* country,
                         GeocodingResponse** response);

/*
 * Search without writing to cache. Useful for endpoints that should not
 * create or update the shared city cache (e.g. autocomplete /cities).
 */
int geocoding_api_search_no_cache(const char* city_name, const char* country,
                                  GeocodingResponse** response);

/* Try to load results from cache first (read-only). If cache is missing or
 * expired, fetch from API but do NOT save results to cache. Returns 0 on
 * success and fills `response` (caller must free). */
int geocoding_api_search_readonly_cache(const char*         city_name,
                                        const char*         country,
                                        GeocodingResponse** response);

/**
 * Smart search with 3-tier fallback strategy
 *
 * Searches in this order:
 * 1. Popular Cities DB (in-memory, fastest)
 * 2. File cache (fast)
 * 3. Open-Meteo API (slow, uses quota)
 *
 * @param query Search query (min 2 characters)
 * @param response Pointer to search result (caller must free)
 * @return 0 on success, < 0 on error
 *
 * This function minimizes API calls for autocomplete by checking
 * local databases first.
 */
int geocoding_api_search_smart(const char* query,
                              GeocodingResponse** response);

/**
 * Search for a city by name with an additional region filter
 *
 * @param city_name City name
 * @param region Region / province (optional)
 * @param country Country code (optional)
 * @param response Pointer to the search result
 * @return 0 on success, < 0 on error
 *
 * Example:
 *   geocoding_api_search_detailed("Lviv", "Lviv Oblast", "UA", &response);
 */
int geocoding_api_search_detailed(const char* city_name, const char* region,
                                  const char*         country,
                                  GeocodingResponse** response);

/**
 * Get the best result (the one with the largest population)
 *
 * @param response Search response
 * @return Pointer to the best result or NULL
 */
GeocodingResult* geocoding_api_get_best_result(GeocodingResponse* response,
                                               const char*        country);

/**
 * Free the memory for a search response
 *
 * @param response Response to free
 */
void geocoding_api_free_response(GeocodingResponse* response);

/**
 * Clear the cache
 *
 * @return 0 on success
 */
int geocoding_api_clear_cache(void);

/**
 * Cleanup the geocoding module
 */
void geocoding_api_cleanup(void);

/**
 * Format a result into a readable string
 *
 * @param result Geocoding result
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return 0 on success
 *
 * Example output: "Kyiv, Kyiv Oblast, Ukraine (50.4501, 30.5234)"
 */
int geocoding_api_format_result(GeocodingResult* result, char* buffer,
                                size_t buffer_size);

#endif /* GEOCODING_API_H */