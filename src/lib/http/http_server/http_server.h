
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "http_server_connection.h"
#include "smw.h"
#include "tcp_server.h"

typedef int (*HttpServerOnConnection)(void*                 context,
                                      HTTPServerConnection* connection);

typedef struct {
    HttpServerOnConnection onConnection;

    TCPServer tcpServer;
    SmwTask*  task;

} HTTPServer;

int http_server_initiate(HTTPServer*            server,
                         HttpServerOnConnection on_connection);
int http_server_initiate_ptr(HttpServerOnConnection on_connection,
                             HTTPServer**           server_ptr);

void http_server_dispose(HTTPServer* server);
void http_server_dispose_ptr(HTTPServer** server_ptr);

#endif // HTTP_SERVER_H
