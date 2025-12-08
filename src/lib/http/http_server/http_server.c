#include "http_server.h"

#include <stdio.h>
#include <stdlib.h>

//-----------------Internal Functions-----------------

void http_server_task_work(void* context, uint64_t mon_time);
int  http_server_on_accept(int fd, void* context);

//----------------------------------------------------

int http_server_initiate(HTTPServer*            server,
                         HttpServerOnConnection on_connection) {
    server->onConnection = on_connection;

    tcp_server_initiate(&server->tcpServer, "10680", http_server_on_accept,
                        server);

    server->task = smw_create_task(server, http_server_task_work);

    return 0;
}

int http_server_initiate_ptr(HttpServerOnConnection on_connection,
                             HTTPServer**           server_ptr) {
    if (server_ptr == NULL) {
        return -1;
    }

    HTTPServer* server = (HTTPServer*)malloc(sizeof(HTTPServer));
    if (server == NULL) {
        return -2;
    }

    int result = http_server_initiate(server, on_connection);
    if (result != 0) {
        free(server);
        return result;
    }

    *(server_ptr) = server;

    return 0;
}

int http_server_on_accept(int fd, void* context) {
    HTTPServer* server = (HTTPServer*)context;

    HTTPServerConnection* connection = NULL;
    int result = http_server_connection_initiate_ptr(fd, &connection);
    if (result != 0) {
        printf("HTTPServer_OnAccept: Failed to initiate connection\n");
        return -1;
    }

    server->onConnection(server, connection);

    return 0;
}

void http_server_task_work(void* context, uint64_t mon_time) {
    // HTTPServer* _Server = (HTTPServer*)_Context;
}

void http_server_dispose(HTTPServer* server) {
    tcp_server_dispose(&server->tcpServer);
    smw_destroy_task(server->task);
}

void http_server_dispose_ptr(HTTPServer** server_ptr) {
    if (server_ptr == NULL || *(server_ptr) == NULL) {
        return;
    }

    http_server_dispose(*(server_ptr));
    free(*(server_ptr));
    *(server_ptr) = NULL;
}
