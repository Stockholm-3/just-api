/**
 * @file elpris_api.h
 * @brief Asynchronous HTTP API for fetching Swedish electricity prices.
 *
 * This module exposes a single entry point that:
 *  - Parses HTTP query parameters
 *  - Resolves the correct price date using Swedish local time rules
 *  - Fetches electricity prices from the public elprisetjustnu.se API
 *  - Uses file-based caching for both latest and historical data
 *  - Sends an HTTP response asynchronously via the provided server connection
 *
 * The API is designed to be called from an HTTP server request handler.
 */

#ifndef ELPRIS_API_H
#define ELPRIS_API_H

#include <http_server_connection.h>

/**
 * @brief Fetch electricity prices and respond asynchronously over HTTP.
 *
 * This function parses the provided query string, determines the correct
 * price date, and either serves cached data or fetches fresh data from the
 * upstream electricity price API.
 *
 * ## Query parameters
 * The following query parameters are supported:
 *
 * - `price` (required):
 *   Electricity price area (e.g. `"SE1"`, `"SE2"`, `"SE3"`, `"SE4"`).
 *
 * - `date` (optional):
 *   Date in ISO format `YYYY-MM-DD`.
 *   If omitted, the function automatically selects the "latest available"
 *   price date according to Swedish local time:
 *     - Before 13:00 CET/CEST → today
 *     - From 13:00 onward     → tomorrow
 *
 * ## Caching behavior
 * - Latest price data is cached until the next Swedish 13:00 cutoff.
 * - Historical price data is cached for a long duration (effectively
 * permanent).
 *
 * ## Response behavior
 * - On success, sends a `200 OK` response with `application/json` content.
 * - On client errors (invalid query), sends a `400 Bad Request` JSON error.
 * - On upstream or internal errors, sends a `503 Service Unavailable` JSON
 * error.
 *
 * ## Asynchronous semantics
 * - The HTTP request to the upstream API is performed asynchronously.
 * - All responses (success or error) are sent from the async callback.
 * - The caller must not reuse or free the `HTTPServerConnection` until a
 *   response has been sent.
 *
 * @param conn
 *   Active HTTP server connection used to send the response.
 *   Must not be NULL.
 *
 * @param query
 *   Raw query string from the HTTP request.
 *   May be NULL or empty (treated as invalid).
 *   Example: `"?price=SE3&date=2024-01-15"`
 *
 * @return
 *   0 on successful initiation of the request (including cache hits).
 *  -1 if an immediate error occurs (invalid input, memory allocation failure,
 *     or inability to start the HTTP request).
 *   Note: asynchronous failures are reported via HTTP error responses, not
 *   through this return value.
 */
int elpris_api_fetch_and_respond(HTTPServerConnection* conn, const char* query);

#endif /* ELPRIS_API_H */
