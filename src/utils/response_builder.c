/**
 * @file response_builder.c
 * @brief Implementation of the standardized HTTP JSON response builder.
 *
 * This file provides the implementation for building consistent JSON
 * responses across all weather API endpoints using the Jansson library.
 *
 * @see response_builder.h for the public interface
 */

#include "response_builder.h"

/**
 * @brief Build a standardized success response.
 *
 * Creates a JSON object with "success": true and includes the provided
 * data object. The response is formatted with 2-space indentation.
 *
 * @param[in] data_object JSON object to include in the response.
 *
 * @return Allocated JSON string, or NULL on failure.
 */
char* response_builder_success(json_t* data_object) {
    if (!data_object) {
        return NULL;
    }

    json_t* root = json_object();
    if (!root) {
        return NULL;
    }

    json_object_set_new(root, "success", json_true());
    json_object_set_new(root, "data", data_object);

    char* json_str = json_dumps(root, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
    json_decref(root);

    return json_str;
}

/**
 * @brief Build a standardized error response.
 *
 * Creates a JSON object with "success": false and an error object
 * containing the status code, type, and message.
 *
 * @param[in] code       HTTP status code.
 * @param[in] error_type Human-readable error type.
 * @param[in] message    Detailed error message.
 *
 * @return Allocated JSON string, or NULL on failure.
 */
char* response_builder_error(int code, const char* error_type,
                             const char* message) {
    if (!error_type || !message) {
        return NULL;
    }

    json_t* root = json_object();
    if (!root) {
        return NULL;
    }

    json_object_set_new(root, "success", json_false());

    json_t* error_obj = json_object();
    json_object_set_new(error_obj, "code", json_integer(code));
    json_object_set_new(error_obj, "type", json_string(error_type));
    json_object_set_new(error_obj, "message", json_string(message));

    json_object_set_new(root, "error", error_obj);

    char* json_str = json_dumps(root, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
    json_decref(root);

    return json_str;
}

/**
 * @brief Get the error type string for an HTTP status code.
 *
 * Maps common HTTP status codes to their standard descriptions.
 *
 * @param[in] code HTTP status code.
 *
 * @return Static constant string with the error type description.
 */
const char* response_builder_get_error_type(int code) {
    switch (code) {
    case HTTP_BAD_REQUEST:
        return "Bad Request";
    case HTTP_NOT_FOUND:
        return "Not Found";
    case HTTP_INTERNAL_ERROR:
        return "Internal Server Error";
    case HTTP_OK:
        return "OK";
    default:
        return "Unknown Error";
    }
}
