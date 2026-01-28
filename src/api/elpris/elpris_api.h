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
 *        On error or timeout, this may contain an error message instead.
 *        The ownership of this pointer is NOT transferred to the caller;
 *        the data is only valid for the duration of the callback.
 *
 * @param context
 *        User-defined context pointer passed to fetch_elpris_async().
 *
 * @return
 *        Currently unused. The return value is ignored by the API.
 *        (Reserved for future use.)
 */
typedef int (*ElprisApiOnResponse)(char* json_data, void* context);

/**
 * @brief Represents a single electricity price interval.
 *
 * This structure corresponds to one hourly price point returned by
 * the Elpris API.
 *
 * @note
 * Parsing from JSON into this structure is NOT implemented yet.
 * At the moment, raw JSON is forwarded directly to the user callback.
 */
typedef struct {
    float sek_per_kwh;    /**< Electricity price in SEK per kWh */
    char  start_time[32]; /**< ISO-8601 formatted start time string */
    char  end_time[32];   /**< ISO-8601 formatted end time string */
} PricePoint;

/**
 * @brief Container for a full day's electricity prices.
 *
 * The Elpris API typically returns up to 24 hourly entries,
 * but this array allows up to 96 entries for future compatibility
 * (e.g. 15-minute resolution).
 *
 * @note
 * This structure is currently unused.
 * Automatic population of this struct from JSON is NOT implemented.
 */
typedef struct {
    PricePoint price_points[96]; /**< Array of price points */
} ElprisResponse;

/**
 * @brief Fetch electricity prices asynchronously for a given date.
 *
 * This function initiates an HTTP GET request to the Elpris API for
 * the specified date and price area (currently hardcoded to SE3).
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
 * - Only the SE3 price area is supported at the moment.
 * - The response is provided as raw JSON.
 * - No JSON parsing or validation is performed by this API yet.
 */
int elpris_api_fetch_async(unsigned int year, unsigned int month,
                           unsigned int day, ElprisApiOnResponse callback,
                           void* context);

#endif // ELPRIS_API_H
