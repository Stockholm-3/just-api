#ifndef ELPRIS_API_H
#define ELPRIS_API_H

#include <http_client.h>

/**
 * @brief Callback type invoked when an Elpris API request completes.
 *
 * This callback is called asynchronously when the HTTP request finishes,
 * fails, or times out.
 *
 * @param json_data
 *        Pointer to a null-terminated JSON string returned by the API.
 *        On success, contains valid JSON data.
 *        On error or timeout, this parameter is NULL.
 *        The ownership of this pointer is NOT transferred to the caller;
 *        the data is only valid for the duration of the callback.
 *
 * @param context
 *        User-defined context pointer passed to the fetch function.
 *
 * @return
 *        Currently unused. The return value is ignored by the API.
 */
typedef int (*ElprisApiOnResponse)(char* json_data, void* context);

/**
 * @brief Fetch electricity prices asynchronously for a given date and price
 * area.
 *
 * This function initiates an HTTP GET request to the Elpris API for
 * the specified date and price area.
 *
 * The request is performed asynchronously. The provided callback
 * will be invoked when the request completes, fails, or times out.
 *
 * @param year
 *        Year (e.g. 2024)
 *
 * @param month
 *        Month (1–12)
 *
 * @param day
 *        Day of month (1–31)
 *
 * @param price_group
 *        3-character price area code (e.g., "SE1", "SE2", "SE3", "SE4")
 *
 * @param callback
 *        User callback function to receive the response.
 *        Must not be NULL.
 *
 * @param context
 *        Optional user-defined pointer passed through to the callback.
 *
 * @return
 *        0 or positive value on successful request initiation.
 *        -1 on parameter validation or memory allocation failure.
 *
 * @note
 * - The response is provided as raw JSON.
 * - On error, the callback receives NULL.
 * - The function validates date parameters but not price_group format.
 */
int elpris_api_fetch_async(unsigned int year, unsigned int month,
                           unsigned int day, char price_group[3],
                           ElprisApiOnResponse callback, void* context);

/**
 * @brief Fetch electricity prices using a query string format.
 *
 * Parses a query string and initiates an HTTP request to the Elpris API.
 * Query format: "date=YYYY-MM-DD&price=XXX"
 * Example: "date=2024-12-31&price=SE3"
 *
 * The request is performed asynchronously. The provided callback
 * will be invoked when the request completes, fails, or times out.
 *
 * @param query
 *        Query string containing date and price parameters.
 *        May optionally start with '?'.
 *        Must not be NULL.
 *
 * @param callback
 *        User callback function to receive the response.
 *        Must not be NULL.
 *
 * @param context
 *        Optional user-defined pointer passed through to the callback.
 *
 * @return
 *        0 or positive value on successful request initiation.
 *        -1 on parameter validation, parsing error, or memory allocation
 * failure.
 *
 * @note
 * - On parsing errors, the callback is invoked immediately with NULL.
 * - Price group must be 2-3 characters (e.g., "SE", "SE1", "SE2").
 * - Date format must be YYYY-MM-DD.
 */
int elpris_api_fetch_query_async(const char*         query,
                                 ElprisApiOnResponse callback, void* context);

#endif // ELPRIS_API_H
