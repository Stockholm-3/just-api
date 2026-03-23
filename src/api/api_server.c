/**
 * @file api_server.c
 * @brief Implementation of the api HTTP server.
 *
 * This file implements the apiServer lifecycle management and
 * internal callback functions for handling HTTP connections.
 *
 * @see api_server.h for the public interface
 */

#include "api_server.h"

#include "api_server_instance.h"
#include "utils.h"

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
 * @param[in] context  apiServer pointer cast to void*.
 * @param[in] mon_time Current scheduler time in ticks.
 */
void api_server_task_work(void* context, uint64_t mon_time);

/**
 * @brief HTTP connection callback for new client connections.
 * @internal
 *
 * Called by the HTTP server when a new client connects.
 * Creates a new apiServerInstance to handle the connection.
 *
 * @param[in] context    apiServer pointer cast to void*.
 * @param[in] connection The new HTTP connection to handle.
 *
 * @return 0 on success, -1 on failure.
 */
int api_server_on_http_connection(void*                 context,
                                  HTTPServerConnection* connection);

/* ============= Public API Implementation ============= */

/**
 * @brief Initialize a apiServer structure.
 *
 * @param[in,out] server Server to initialize.
 *
 * @return 0 on success.
 */
int api_server_initiate(ApiServer* server, ThreadPool* request_pool) {
    http_server_initiate(&server->httpServer, api_server_on_http_connection);

    server->instances    = linked_list_create();
    server->request_pool = request_pool;

    server->task = smw_create_task(server, api_server_task_work);

    return 0;
}

/**
 * @brief Allocate and initialize a apiServer dynamically.
 *
 * @param[out] server_ptr Pointer to receive the allocated server.
 *
 * @return 0 on success, -1 if server_ptr is NULL, -2 if allocation fails.
 */
int api_server_initiate_ptr(ApiServer** server_ptr, ThreadPool* request_pool) {
    if (server_ptr == NULL) {
        return -1;
    }

    ApiServer* server = (ApiServer*)malloc(sizeof(ApiServer));
    if (server == NULL) {
        return -2;
    }

    int result = api_server_initiate(server, request_pool);
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
 * @param[in] context    apiServer pointer.
 * @param[in] connection New HTTP connection.
 *
 * @return 0 on success, -1 on failure.
 */
int api_server_on_http_connection(void*                 context,
                                  HTTPServerConnection* connection) {
    ApiServer* server = (ApiServer*)context;

    ApiServerInstance* instance = NULL;
    int result = api_server_instance_initiate_ptr(connection, &instance);
    if (result != 0) {
        printf("apiServer_OnHTTPConnection: Failed to initiate instance\n");
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
 * @param[in] context  apiServer pointer.
 * @param[in] mon_time Current scheduler time.
 */
void api_server_task_work(void* context, uint64_t mon_time) {
    ApiServer* server = (ApiServer*)context;

    LinkedList_foreach(server->instances, node) {
        ApiServerInstance* instance = (ApiServerInstance*)node->item;
        api_server_instance_work(instance, mon_time);
    }
}

/* ============= Cleanup Functions ============= */

/**
 * @brief Dispose of a stack-allocated apiServer.
 *
 * @param[in] server Server to dispose.
 */
void api_server_dispose(ApiServer* server) {
    /* Cleanup all instances to prevent memory leak */
    LinkedList_foreach(server->instances, node) {
        ApiServerInstance* instance = (ApiServerInstance*)node->item;
        api_server_instance_dispose(instance);
    }
    linked_list_dispose(&server->instances, free);

    http_server_dispose(&server->httpServer);
    smw_destroy_task(server->task);
}

/**
 * @brief Dispose and free a dynamically allocated apiServer.
 *
 * @param[in,out] server_ptr Pointer to the server pointer (set to NULL).
 */
void api_server_dispose_ptr(ApiServer** server_ptr) {
    if (server_ptr == NULL || *(server_ptr) == NULL) {
        return;
    }

    api_server_dispose(*(server_ptr));
    free(*(server_ptr));
    *(server_ptr) = NULL;
}
