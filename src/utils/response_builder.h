/**
 * @file response_builder.h
 * @brief Standardized HTTP JSON response builder for the weather API.
 *
 * This module provides helper functions to build consistent JSON responses
 * across all API endpoints. It ensures a uniform response format for both
 * successful responses and errors.
 *
 * @par Success Response Format:
 * @code{.json}
 * {
 *   "success": true,
 *   "data": { ... }
 * }
 * @endcode
 *
 * @par Error Response Format:
 * @code{.json}
 * {
 *   "success": false,
 *   "error": {
 *     "code": 400,
 *     "type": "Bad Request",
 *     "message": "Detailed error description"
 *   }
 * }
 * @endcode
 *
 * @note All returned strings must be freed by the caller.
 */

#ifndef RESPONSE_BUILDER_H
#define RESPONSE_BUILDER_H

#include <jansson.h>

/**
 * @defgroup http_status_codes HTTP Status Codes
 * @brief Standard HTTP status codes used in API responses.
 * @{
 */

/** @brief HTTP 200 OK - Request succeeded */
#define HTTP_OK 200

/** @brief HTTP 400 Bad Request - Invalid request parameters */
#define HTTP_BAD_REQUEST 400

/** @brief HTTP 404 Not Found - Resource or endpoint not found */
#define HTTP_NOT_FOUND 404

/** @brief HTTP 500 Internal Server Error - Server-side failure */
#define HTTP_INTERNAL_ERROR 500

/** @} */

/**
 * @brief Build a standardized success response.
 *
 * Creates a JSON response with success=true and the provided data object.
 * The data object ownership is transferred to this function.
 *
 * @param[in] data_object The JSON object containing endpoint-specific data.
 *                        Ownership is transferred; do not free separately.
 *                        Must not be NULL.
 *
 * @return Allocated JSON string on success (caller must free), or NULL on
 * error.
 *
 * @par Example:
 * @code{.c}
 * json_t* data = json_object();
 * json_object_set_new(data, "temperature", json_real(25.5));
 * char* response = response_builder_success(data);
 * // Use response...
 * free(response);
 * @endcode
 */
char* response_builder_success(json_t* data_object);

/**
 * @brief Build a standardized error response.
 *
 * Creates a JSON response with success=false and an error object
 * containing the code, type, and detailed message.
 *
 * @param[in] code       HTTP status code (e.g., 400, 404, 500).
 * @param[in] error_type Error type string (e.g., "Bad Request").
 *                       Must not be NULL.
 * @param[in] message    Detailed error message for debugging.
 *                       Must not be NULL.
 *
 * @return Allocated JSON string on success (caller must free), or NULL on
 * error.
 *
 * @par Example:
 * @code{.c}
 * char* response = response_builder_error(
 *     HTTP_BAD_REQUEST,
 *     response_builder_get_error_type(HTTP_BAD_REQUEST),
 *     "Missing required parameter: lat"
 * );
 * // Use response...
 * free(response);
 * @endcode
 */
char* response_builder_error(int code, const char* error_type,
                             const char* message);

/**
 * @brief Get the error type string for an HTTP status code.
 *
 * Maps HTTP status codes to human-readable error type strings.
 *
 * @param[in] code HTTP status code.
 *
 * @return Constant string describing the error type:
 *         - HTTP_OK (200): "OK"
 *         - HTTP_BAD_REQUEST (400): "Bad Request"
 *         - HTTP_NOT_FOUND (404): "Not Found"
 *         - HTTP_INTERNAL_ERROR (500): "Internal Server Error"
 *         - Other codes: "Unknown Error"
 *
 * @note The returned string is a static constant and must not be freed.
 */
const char* response_builder_get_error_type(int code);

#endif /* RESPONSE_BUILDER_H */
