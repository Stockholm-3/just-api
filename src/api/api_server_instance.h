/**
 * @file api_server_instance.h
 * @brief api server instance management for HTTP connections.
 *
 * This module provides lifecycle management for individual api server
 * instances. Each apiServerInstance wraps an HTTPServerConnection and
 * handles HTTP request processing for all endpoints.
 *
 * The module supports both stack-allocated and heap-allocated instances,
 * with corresponding initialization and cleanup functions for each allocation
 * method.
 *
 * @par Supported Endpoints:
 * - GET / - Homepage with API documentation
 * - GET/POST /echo - Echo endpoint for debugging
 * - GET /v1/current?lat=XX&lon=YY - Current weather by coordinates
 * - GET /v1/weather?city=NAME&country=CODE - Weather by city name
 * - GET /v1/cities?query=SEARCH - City search for autocomplete
 *
 * @note Instances must be properly initialized before use and disposed of
 *       when no longer needed to prevent resource leaks.
 *
 * @see http_server_connection.h for the underlying connection handling
 * @see api_server.h for the parent server that manages instances
 */

#ifndef API_SERVER_INSTANCE_H
#define API_SERVER_INSTANCE_H

#include "http_server_connection.h"

/**
 * @brief api server instance for handling a single HTTP connection.
 *
 * Wraps an HTTPServerConnection and provides request routing and
 * response generation for api API endpoints.
 */
typedef struct {
    /** @brief Pointer to the underlying HTTP connection. */
    HTTPServerConnection* connection;
} ApiServerInstance;

/**
 * @brief Initialize a stack-allocated apiServerInstance.
 *
 * Associates the instance with the given HTTP connection and registers
 * the request callback handler for processing incoming HTTP requests.
 *
 * @param[in,out] instance   Pointer to the apiServerInstance to initialize.
 *                           Must be valid, non-NULL memory.
 * @param[in]     connection Pointer to the HTTPServerConnection to handle.
 *                           The instance does not take ownership.
 *
 * @return 0 on success, non-zero on failure.
 *
 * @note The instance must be disposed with api_server_instance_dispose()
 *       when no longer needed.
 *
 * @par Example:
 * @code{.c}
 * apiServerInstance instance;
 * if (api_server_instance_initiate(&instance, connection) == 0) {
 *     // Instance is ready to handle requests
 *     api_server_instance_dispose(&instance);
 * }
 * @endcode
 */
int api_server_instance_initiate(ApiServerInstance*    instance,
                                 HTTPServerConnection* connection);

/**
 * @brief Allocate and initialize a apiServerInstance dynamically.
 *
 * Allocates memory for a apiServerInstance, initializes it using
 * api_server_instance_initiate(), and sets the output pointer.
 * On initialization failure, the allocated memory is automatically freed.
 *
 * @param[in]  connection   Pointer to the HTTPServerConnection to handle.
 * @param[out] instance_ptr Pointer to receive the allocated instance.
 *                          Set to the new instance on success.
 *
 * @return
 * - 0 on success
 * - -1 if instance_ptr is NULL
 * - -2 if memory allocation fails
 * - Other non-zero values from api_server_instance_initiate()
 *
 * @note The instance must be disposed with
 * api_server_instance_dispose_ptr() when no longer needed.
 *
 * @par Example:
 * @code{.c}
 * apiServerInstance* instance = NULL;
 * if (api_server_instance_initiate_ptr(connection, &instance) == 0) {
 *     // Instance is ready
 *     api_server_instance_dispose_ptr(&instance);
 * }
 * @endcode
 */
int api_server_instance_initiate_ptr(HTTPServerConnection* connection,
                                     ApiServerInstance**   instance_ptr);

/**
 * @brief Periodic work function for the instance.
 *
 * Called by the apiServer scheduler task for each active instance.
 * Can be used for implementing timeouts, connection cleanup, or other
 * periodic maintenance tasks.
 *
 * @param[in] instance Pointer to the apiServerInstance.
 * @param[in] mon_time Current scheduler time in ticks.
 *
 * @note Currently a no-op, reserved for future functionality such as
 *       request timeouts or keep-alive management.
 */
void api_server_instance_work(ApiServerInstance* instance, uint64_t mon_time);

/**
 * @brief Dispose of a stack-allocated apiServerInstance.
 *
 * Releases any resources owned by the instance, such as temporary
 * buffers or timers. The underlying connection is not disposed
 * (it is owned by the HTTP server).
 *
 * @param[in] instance Pointer to the apiServerInstance to dispose.
 *
 * @note Currently a no-op, reserved for future cleanup logic.
 * @note After calling this function, the instance should not be used
 *       unless re-initialized.
 */
void api_server_instance_dispose(ApiServerInstance* instance);

/**
 * @brief Dispose and free a dynamically allocated apiServerInstance.
 *
 * Calls api_server_instance_dispose() to clean up the instance,
 * then frees the allocated memory and sets the pointer to NULL.
 *
 * @param[in,out] instance_ptr Pointer to the apiServerInstance pointer.
 *                             The pointed-to pointer is set to NULL after
 * disposal. Safe to call with NULL or pointer to NULL.
 *
 * @par Example:
 * @code{.c}
 * apiServerInstance* instance = NULL;
 * api_server_instance_initiate_ptr(connection, &instance);
 * // Use instance...
 * api_server_instance_dispose_ptr(&instance);
 * // instance is now NULL
 * @endcode
 */
void api_server_instance_dispose_ptr(ApiServerInstance** instance_ptr);

#endif // API_SERVER_INSTANCE_H
