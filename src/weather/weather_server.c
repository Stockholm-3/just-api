/**
 * @file weather_server.c
 * @brief Implementation of the weather HTTP server.
 *
 * This file implements the WeatherServer lifecycle management and
 * internal callback functions for handling HTTP connections.
 *
 * @see weather_server.h for the public interface
 */

#include "weather_server.h"

#include "weather_server_instance.h"

#include <stdio.h>
#include <stdlib.h>

/* ============= Internal Function Declarations ============= */

/**
 * @brief Scheduler task callback for periodic instance work.
 * @internal
 *
 * Called periodically by the scheduler to perform maintenance
 * work on all active server instances.
 *
 * @param[in] context  WeatherServer pointer cast to void*.
 * @param[in] mon_time Current scheduler time in ticks.
 */
void weather_server_task_work(void* context, uint64_t mon_time);

/**
 * @brief HTTP connection callback for new client connections.
 * @internal
 *
 * Called by the HTTP server when a new client connects.
 * Creates a new WeatherServerInstance to handle the connection.
 *
 * @param[in] context    WeatherServer pointer cast to void*.
 * @param[in] connection The new HTTP connection to handle.
 *
 * @return 0 on success, -1 on failure.
 */
int weather_server_on_http_connection(void*                 context,
                                      HTTPServerConnection* connection);

/* ============= Public API Implementation ============= */

/**
 * @brief Initialize a WeatherServer structure.
 *
 * @param[in,out] server Server to initialize.
 *
 * @return 0 on success.
 */
int weather_server_initiate(WeatherServer* server) {
    http_server_initiate(&server->httpServer,
                         weather_server_on_http_connection);

    server->instances = linked_list_create();

    server->task = smw_create_task(server, weather_server_task_work);

    return 0;
}

/**
 * @brief Allocate and initialize a WeatherServer dynamically.
 *
 * @param[out] server_ptr Pointer to receive the allocated server.
 *
 * @return 0 on success, -1 if server_ptr is NULL, -2 if allocation fails.
 */
int weather_server_initiate_ptr(WeatherServer** server_ptr) {
    if (server_ptr == NULL) {
        return -1;
    }

    WeatherServer* server = (WeatherServer*)malloc(sizeof(WeatherServer));
    if (server == NULL) {
        return -2;
    }

    int result = weather_server_initiate(server);
    if (result != 0) {
        free(server);
        return result;
    }

    *(server_ptr) = server;

    return 0;
}

/* ============= Internal Callback Implementations ============= */

/**
 * @brief Handle new HTTP connection by creating a server instance.
 * @internal
 *
 * @param[in] context    WeatherServer pointer.
 * @param[in] connection New HTTP connection.
 *
 * @return 0 on success, -1 on failure.
 */
int weather_server_on_http_connection(void*                 context,
                                      HTTPServerConnection* connection) {
    WeatherServer* server = (WeatherServer*)context;

    WeatherServerInstance* instance = NULL;
    int result = weather_server_instance_initiate_ptr(connection, &instance);
    if (result != 0) {
        printf("WeatherServer_OnHTTPConnection: Failed to initiate instance\n");
        return -1;
    }

    linked_list_append(server->instances, instance);

    return 0;
}

/**
 * @brief Periodic work callback for all server instances.
 * @internal
 *
 * Iterates through all active instances and calls their work function.
 *
 * @param[in] context  WeatherServer pointer.
 * @param[in] mon_time Current scheduler time.
 */
void weather_server_task_work(void* context, uint64_t mon_time) {
    WeatherServer* server = (WeatherServer*)context;

    LinkedList_foreach(server->instances, node) {
        WeatherServerInstance* instance = (WeatherServerInstance*)node->item;
        weather_server_instance_work(instance, mon_time);
    }
}

/* ============= Cleanup Functions ============= */

/**
 * @brief Dispose of a stack-allocated WeatherServer.
 *
 * @param[in] server Server to dispose.
 */
void weather_server_dispose(WeatherServer* server) {
    /* Cleanup all instances to prevent memory leak */
    LinkedList_foreach(server->instances, node) {
        WeatherServerInstance* instance = (WeatherServerInstance*)node->item;
        weather_server_instance_dispose(instance);
    }
    linked_list_dispose(&server->instances, free);

    http_server_dispose(&server->httpServer);
    smw_destroy_task(server->task);
}

/**
 * @brief Dispose and free a dynamically allocated WeatherServer.
 *
 * @param[in,out] server_ptr Pointer to the server pointer (set to NULL).
 */
void weather_server_dispose_ptr(WeatherServer** server_ptr) {
    if (server_ptr == NULL || *(server_ptr) == NULL) {
        return;
    }

    weather_server_dispose(*(server_ptr));
    free(*(server_ptr));
    *(server_ptr) = NULL;
}
