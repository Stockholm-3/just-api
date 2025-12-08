/**
 * response_builder.c - Implementation of standardized response builder
 */

#include "response_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
