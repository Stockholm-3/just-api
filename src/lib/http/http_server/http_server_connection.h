/// OnRequest callback will send context with data fields from request.
/// In this callback you can set the write_buffer with the response you want.
#ifndef HTTP_SERVER_CONNECTION_H
#define HTTP_SERVER_CONNECTION_H

#include "smw.h"
#include "tcp_client.h"

#include <stddef.h>
#include <stdint.h>

// Max chunks to read per iteration
#define CHUNK_SIZE 256

// Headers max lengths
#define METHOD_MAX_LEN 9
#define REQUEST_PATH_MAX_LEN 256
#define HOST_MAX_LEN 256

typedef int (*HttpServerConnectionOnRequest)(void* context);

typedef enum {
    HTTP_SERVER_CONNECTION_STATE_SEND,
    HTTP_SERVER_CONNECTION_STATE_RECEIVE,
    HTTP_SERVER_CONNECTION_STATE_DISPOSE,
} HttpServerConnectionState;

typedef struct {
    TCPClient tcpClient;

    SmwTask*                      task;
    HttpServerConnectionState     state;
    void*                         context;
    HttpServerConnectionOnRequest onRequest;

    char*  method;
    char*  request_path;
    char*  host;
    size_t content_len;

    uint8_t* read_buffer;
    size_t   read_buffer_size;

    uint8_t* body;
    size_t   body_start;

    uint8_t* write_buffer;
    size_t   write_size;
    size_t   write_offset;

} HTTPServerConnection;

int http_server_connection_initiate(HTTPServerConnection* connection, int fd);
int http_server_connection_initiate_ptr(int                    fd,
                                        HTTPServerConnection** connection_ptr);

void http_server_connection_set_callback(
    HTTPServerConnection* connection, void* context,
    HttpServerConnectionOnRequest on_request);

void http_server_connection_dispose(HTTPServerConnection* connection);
void http_server_connection_dispose_ptr(HTTPServerConnection** connection_ptr);

#endif // HTTP_SERVER_CONNECTION_H
