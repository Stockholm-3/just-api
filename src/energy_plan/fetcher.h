#ifndef FETCHER_H
#define FETCHER_H

#include "file_cache.h"

#include <stdint.h>

/**
 * @file fetcher.h
 * @brief Pure HTTP fetch workers for weather and electricity price data.
 *
 * Each function accepts a caller-owned @ref FileCacheInstance and performs
 * only HTTP requests and cache reads/writes. Cache configuration, TTL, and
 * lifecycle are the responsibility of the caller (typically fetch_scheduler).
 */

/**
 * @brief Fetch and cache minutely weather forecasts for all registered cities.
 *
 * Loads the city registry, then for each city checks the provided cache. If a
 * valid entry exists it is skipped; otherwise a forecast is fetched from the
 * local weather service and saved under the key "<city>-<lat>-<lon>".
 *
 * Blocks the calling thread for the duration of all HTTP requests.
 *
 * @param cache  Caller-owned cache instance. Must not be NULL.
 * @param port   Local service port string (e.g. "10680"). Must not be NULL.
 * @param timeout_ms  Per-request HTTP timeout in milliseconds.
 */
void fetch_all_city_forecasts(FileCacheInstance* cache, const char* port,
                              uint64_t timeout_ms);

/**
 * @brief Fetch electricity prices for all SE price groups and merge into cache.
 *
 * Fetches SE1–SE4 from the local elpris service, merges the results into a
 * single JSON array, and saves it under the key "<cache_key_prefix>_merged".
 *
 * Blocks the calling thread for the duration of all HTTP requests.
 *
 * @param cache             Caller-owned cache instance. Must not be NULL.
 * @param cache_key_prefix  Prefix used to build the merged cache key. Must not
 * be NULL.
 * @param port              Local service port string (e.g. "10680"). Must not
 * be NULL.
 * @param timeout_ms        Per-request HTTP timeout in milliseconds.
 *
 * @return  0  All groups fetched and merged result saved successfully.
 * @return -1  A fatal error occurred (bad arguments or cache failure).
 */
int fetch_all_price_groups(FileCacheInstance* cache,
                           const char* cache_key_prefix, const char* port,
                           uint64_t timeout_ms);
#endif // FETCHER_H
