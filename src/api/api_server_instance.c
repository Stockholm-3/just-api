/**
 * @file api_server_instance.c
 * @brief Implementation of api server instance and HTTP request routing.
 *
 * This file implements the apiServerInstance lifecycle management and
 * the HTTP request handler that routes requests to appropriate endpoint
 * handlers based on the URL path.
 *
 * @see api_server_instance.h for the public interface
 */

#include "api_server_instance.h"

#include "routes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============= Internal Function Declarations ============= */

/**
 * @brief HTTP request callback handler.
 * @internal
 *
 * Routes incoming HTTP requests to the appropriate endpoint handler
 * based on the request method and path.
 *
 * @param[in] context apiServerInstance pointer cast to void*.
 *
 * @return 0 on success, -1 on fatal error.
 */
int api_server_instance_on_request(void* context);

/* ============= Public API Implementation ============= */

/**
 * @brief Initialize a apiServerInstance.
 *
 * @param[in,out] instance   Instance to initialize.
 * @param[in]     connection HTTP connection to associate.
 *
 * @return 0 on success.
 */
int api_server_instance_initiate(ApiServerInstance*    instance,
                                 HTTPServerConnection* connection) {
    instance->connection = connection;

    http_server_connection_set_callback(instance->connection, instance,
                                        api_server_instance_on_request);

    return 0;
}

/**
 * @brief Allocate and initialize a apiServerInstance.
 *
 * @param[in]  connection   HTTP connection to handle.
 * @param[out] instance_ptr Pointer to receive the allocated instance.
 *
 * @return 0 on success, -1 if instance_ptr is NULL, -2 if allocation fails.
 */
int api_server_instance_initiate_ptr(HTTPServerConnection* connection,
                                     ApiServerInstance**   instance_ptr) {
    if (instance_ptr == NULL) {
        return -1;
    }

    ApiServerInstance* instance =
        (ApiServerInstance*)malloc(sizeof(ApiServerInstance));
    if (instance == NULL) {
        return -2;
    }

    int result = api_server_instance_initiate(instance, connection);
    if (result != 0) {
        free(instance);
        return result;
    }

    *(instance_ptr) = instance;

    return 0;
}

/**
 * @brief Callback from http_server_connection that handles all routes
 */
int api_server_instance_on_request(void* context) {
    ApiServerInstance*    inst = (ApiServerInstance*)context;
    HTTPServerConnection* conn = inst->connection;

    char path[256]  = {0};
    char query[512] = {0};

    split_path_and_query(conn->request_path, path, sizeof(path), query,
                         sizeof(query));

    for (size_t i = 0; i < ROUTE_COUNT; i++) {
        if (strcmp(conn->method, g_routes[i].method) == 0 &&
            strcmp(path, g_routes[i].path) == 0) {
            return g_routes[i].handler(conn, query);
        }
    }

    return handle_not_found(conn, path);
}

/* ============= Lifecycle Functions ============= */

/**
 * @brief Periodic work function (currently no-op).
 *
 * @param[in] instance Instance to process.
 * @param[in] mon_time Current scheduler time.
 */
void api_server_instance_work(ApiServerInstance* instance, uint64_t mon_time) {
    /* Reserved for future timeout/cleanup logic */
}

/**
 * @brief Dispose of a stack-allocated instance (currently no-op).
 *
 * @param[in] instance Instance to dispose.
 */
void api_server_instance_dispose(ApiServerInstance* instance) {
    http_server_connection_dispose_ptr(&instance->connection);
}

/**
 * @brief Dispose and free a dynamically allocated instance.
 *
 * @param[in,out] instance_ptr Pointer to instance pointer (set to NULL).
 */
void api_server_instance_dispose_ptr(ApiServerInstance** instance_ptr) {
    if (instance_ptr == NULL || *(instance_ptr) == NULL) {
        return;
    }

    api_server_instance_dispose(*(instance_ptr));
    free(*(instance_ptr));
    *(instance_ptr) = NULL;
}
