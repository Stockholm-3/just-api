/**
 * @file weather_server.h
 * @brief Weather HTTP server main module.
 *
 * This module provides the main WeatherServer structure and lifecycle
 * management functions. The WeatherServer wraps an HTTP server and
 * manages multiple client connections through WeatherServerInstance objects.
 *
 * @par Architecture:
 * - WeatherServer contains an HTTPServer for handling TCP connections
 * - Each client connection creates a WeatherServerInstance
 * - A scheduler task performs periodic work on all active instances
 * - Instances are stored in a linked list for iteration
 *
 * @par Usage:
 * @code{.c}
 * WeatherServer* server = NULL;
 * if (weather_server_initiate_ptr(&server) == 0) {
 *     // Server is running, handle events...
 *     weather_server_dispose_ptr(&server);
 * }
 * @endcode
 *
 * @see weather_server_instance.h for individual connection handling
 * @see http_server.h for the underlying HTTP server implementation
 */

#ifndef WEATHER_SERVER_H
#define WEATHER_SERVER_H

#include "http_server.h"
#include "linked_list.h"
#include "smw.h"

/**
 * @brief Main weather server structure.
 *
 * Contains the HTTP server, connection instances, and scheduler task
 * for managing the weather API server.
 */
typedef struct {
    /** @brief Embedded HTTP server for handling connections. */
    HTTPServer httpServer;

    /** @brief Linked list of active WeatherServerInstance pointers. */
    LinkedList* instances;

    /** @brief Scheduler task for periodic instance work. */
    SmwTask* task;

} WeatherServer;

/**
 * @brief Initialize a stack-allocated WeatherServer.
 *
 * Performs complete initialization of the weather server:
 * - Initializes the embedded HTTP server with connection callback
 * - Creates an empty linked list for client instances
 * - Registers a scheduler task for periodic work
 *
 * @param[in,out] server Pointer to the WeatherServer structure to initialize.
 *                       Must be valid, non-NULL memory.
 *
 * @return 0 on success, non-zero on failure.
 *
 * @note The server must be disposed with weather_server_dispose() when done.
 *
 * @par Example:
 * @code{.c}
 * WeatherServer server;
 * if (weather_server_initiate(&server) == 0) {
 *     // Use server...
 *     weather_server_dispose(&server);
 * }
 * @endcode
 */
int weather_server_initiate(WeatherServer* server);

/**
 * @brief Allocate and initialize a WeatherServer dynamically.
 *
 * Allocates memory for a WeatherServer and initializes it using
 * weather_server_initiate(). On initialization failure, the allocated
 * memory is automatically freed.
 *
 * @param[out] server_ptr Pointer to receive the allocated server.
 *                        Set to the new server on success, unchanged on
 * failure.
 *
 * @return
 * - 0 on success
 * - -1 if server_ptr is NULL
 * - -2 if memory allocation fails
 * - Other non-zero values from weather_server_initiate() on init failure
 *
 * @note The server must be disposed with weather_server_dispose_ptr() when
 * done.
 *
 * @par Example:
 * @code{.c}
 * WeatherServer* server = NULL;
 * int result = weather_server_initiate_ptr(&server);
 * if (result == 0) {
 *     // Use server...
 *     weather_server_dispose_ptr(&server);
 * }
 * @endcode
 */
int weather_server_initiate_ptr(WeatherServer** server_ptr);

/**
 * @brief Shut down and clean up a stack-allocated WeatherServer.
 *
 * Performs complete cleanup of the weather server:
 * - Disposes all active client instances
 * - Frees the instances linked list
 * - Stops and disposes the HTTP server
 * - Destroys the scheduler task
 *
 * @param[in] server Pointer to the WeatherServer to dispose.
 *                   Must have been initialized with weather_server_initiate().
 *
 * @note After calling this function, the server structure should not be used
 *       unless re-initialized.
 */
void weather_server_dispose(WeatherServer* server);

/**
 * @brief Dispose and free a dynamically allocated WeatherServer.
 *
 * Calls weather_server_dispose() to clean up the server, then frees
 * the allocated memory and sets the pointer to NULL.
 *
 * @param[in,out] server_ptr Pointer to the WeatherServer pointer.
 *                           The pointed-to pointer is set to NULL after
 * disposal. Safe to call with NULL or pointer to NULL.
 *
 * @par Example:
 * @code{.c}
 * WeatherServer* server = NULL;
 * weather_server_initiate_ptr(&server);
 * // Use server...
 * weather_server_dispose_ptr(&server);
 * // server is now NULL
 * @endcode
 */
void weather_server_dispose_ptr(WeatherServer** server_ptr);

#endif /* WEATHER_SERVER_H */
