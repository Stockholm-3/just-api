#ifndef ELPRIS_API_H
#define ELPRIS_API_H

#include <http_client.h>

/**
 * @brief Callback type invoked when an Elpris API request completes.
 *
 * Called asynchronously when a request finishes, either from cache or live
 * HTTP.
 *
 * @param json_data
 *        Null-terminated JSON string returned by the API.
 *        - On success, contains valid JSON.
 *        - On error, timeout, or parsing failure, this is NULL.
 *
 *        Ownership is NOT transferred; data is valid only for the duration
 *        of the callback.
 *
 * @param context
 *        User-defined pointer passed to the fetch function.
 *
 * @return
 *        Currently ignored by the API.
 */
typedef int (*ElprisApiOnResponse)(char* json_data, void* context);

/**
 * @brief Fetch electricity prices asynchronously for a specific date and price
 * area.
 *
 * Initiates an asynchronous request to the Elpris API. Results may be returned
 * from an internal cache if available.
 *
 * @param year Year (e.g., 2024)
 * @param month Month (1–12)
 * @param day Day (1–31)
 * @param price_group 3-character price area code (SE1–SE4)
 * @param callback Callback to receive JSON response
 * @param context User-defined pointer passed through to the callback
 *
 * @return
 *        0 or positive on successful request initiation,
 *        -1 on invalid parameters or allocation failure.
 *
 * @note
 * - Historical data (past dates) is cached for ~10 years.
 * - Latest data (current/future day) is cached until the next daily update
 *   at 13:00 Swedish time.
 * - Callback may be invoked immediately if a valid cache entry exists.
 * - On error, callback receives NULL.
 */
int elpris_api_fetch_async(unsigned int year, unsigned int month,
                           unsigned int day, char price_group[3],
                           ElprisApiOnResponse callback, void* context);

/**
 * @brief Fetch electricity prices using a URL query string.
 *
 * Parses a query string and fetches data asynchronously from the Elpris API.
 *
 * Supported query string formats:
 * - "date=YYYY-MM-DD&price=SE3"
 * - "?date=YYYY-MM-DD&price=SE3"
 *
 * Latest prices (no date provided):
 * - A price group MUST be specified.
 * - Before 13:00 Swedish time → fetch yesterday’s prices
 * - After 13:00 Swedish time → fetch tomorrow’s prices
 *
 * @param query Query string containing date and price parameters
 *              (may include leading '?').
 * @param callback Callback to receive JSON response
 * @param context User-defined pointer passed through to the callback
 *
 * @return
 *        0 or positive on successful request initiation,
 *        -1 on parsing error, validation failure, or allocation failure.
 *
 * @note
 * - On parsing errors, callback is invoked immediately with NULL.
 * - Date format must be YYYY-MM-DD.
 * - Price group must be 3 characters (SE1–SE4). Shorter strings may be accepted
 * but are internally padded/truncated.
 * - Latest data requires a price group; NULL or empty query without a price
 * group is invalid.
 * - Historical data is cached for ~10 years; latest data is cached until next
 * 13:00.
 */
int elpris_api_fetch_query_async(const char*         query,
                                 ElprisApiOnResponse callback, void* context);

#endif // ELPRIS_API_H
