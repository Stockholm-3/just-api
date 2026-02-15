#ifndef FETCHER_H
#define FETCHER_H

#include "file_cache.h"

#include <stdint.h>
#define CONFIG "registered_cities.json"
#define CACHE_PATH "fetch/"

int fetch_all_price_groups_sync(FileCacheInstance* cache,
                                const char* cache_key_prefix, const char* port,
                                uint64_t timeout);

// TODO: BELOW
int load_config();
int fetch_forecast();
int write_output();

#endif // FETCHER_H
