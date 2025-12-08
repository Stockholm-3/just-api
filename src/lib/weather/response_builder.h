/**
 * response_builder.h - Standardized HTTP response builder
 *
 * This module provides helper functions to build consistent JSON responses
 * across all API endpoints.
 */

#ifndef RESPONSE_BUILDER_H
#define RESPONSE_BUILDER_H

#include <jansson.h>

/* HTTP status codes */
#define HTTP_OK 200
#define HTTP_BAD_REQUEST 400
#define HTTP_NOT_FOUND 404
#define HTTP_INTERNAL_ERROR 500

/**
 * Build a standardized success response
 *
 * @param data_object The JSON object containing endpoint-specific data
 * @return JSON string (caller must free), or NULL on error
 */
char* response_builder_success(json_t* data_object);

/**
 * Build a standardized error response
 *
 * @param code HTTP status code (400, 404, 500, etc.)
 * @param error_type Error type string ("Bad Request", "Not Found", etc.)
 * @param message Detailed error message
 * @return JSON string (caller must free), or NULL on error
 */
char* response_builder_error(int code, const char* error_type,
                             const char* message);

/**
 * Helper: Get error type string from HTTP status code
 */
const char* response_builder_get_error_type(int code);

#endif /* RESPONSE_BUILDER_H */
