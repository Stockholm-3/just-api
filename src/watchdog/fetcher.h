#ifndef FETCHER_H
#define FETCHER_H

#include <stdint.h>
#define CONFIG "registered_cities.json"
#define CACHE_PATH "fetch/"

/* One entry from JSON file */
typedef struct {
    char* city;
    char* price;
} CityPrice;

/*
 * Fetch all price groups (SE1–SE4) from server,
 * merge JSON, and save to file.
 *
 * Parameters:
 *   output_path - Output JSON file path
 *   port        - Server port (string)
 *   timeout     - Timeout in ms
 *
 * Returns:
 *   0  on success
 *  -1  on failure
 */
int fetch_all_price_groups_sync(const char* output_path, const char* port,
                                uint64_t timeout);

// TODO: BELOW
int load_config();
int fetch_forecast();
int write_output();

#endif // FETCHER_H
