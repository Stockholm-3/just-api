#include "weather_server.h"

#include "weather_server_instance.h"

#include <stdio.h>
#include <stdlib.h>

//-----------------Internal Functions-----------------

void weather_server_task_work(void* context, uint64_t mon_time);
int  weather_server_on_http_connection(void*                 context,
                                       HTTPServerConnection* connection);

//----------------------------------------------------

int weather_server_initiate(WeatherServer* server) {
    http_server_initiate(&server->httpServer,
                         weather_server_on_http_connection);

    server->instances = linked_list_create();

    server->task = smw_create_task(server, weather_server_task_work);

    return 0;
}

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

void weather_server_task_work(void* context, uint64_t mon_time) {
    WeatherServer* server = (WeatherServer*)context;

    LinkedList_foreach(server->instances, node) {
        WeatherServerInstance* instance = (WeatherServerInstance*)node->item;
        weather_server_instance_work(instance, mon_time);
    }
}

void weather_server_dispose(WeatherServer* server) {
    http_server_dispose(&server->httpServer);
    smw_destroy_task(server->task);
}

void weather_server_dispose_ptr(WeatherServer** server_ptr) {
    if (server_ptr == NULL || *(server_ptr) == NULL) {
        return;
    }

    weather_server_dispose(*(server_ptr));
    free(*(server_ptr));
    *(server_ptr) = NULL;
}
