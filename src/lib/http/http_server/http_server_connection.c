#include "http_server_connection.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-----------------Internal Functions-----------------

void http_server_connection_task_work(void* context, uint64_t mon_time);

//----------------------------------------------------

int http_server_connection_initiate(HTTPServerConnection* connection, int fd) {
    tcp_client_initiate(&connection->tcpClient, fd);
    connection->read_buffer      = NULL;
    connection->method           = NULL;
    connection->request_path     = NULL;
    connection->host             = NULL;
    connection->write_buffer     = NULL;
    connection->body             = NULL;
    connection->read_buffer_size = 0;
    connection->content_len      = 0;
    connection->write_size       = 0;
    connection->write_offset     = 0;
    connection->body_start       = 0;
    connection->state            = HTTP_SERVER_CONNECTION_STATE_RECEIVE;

    connection->task =
        smw_create_task(connection, http_server_connection_task_work);

    return 0;
}

int http_server_connection_initiate_ptr(int                    fd,
                                        HTTPServerConnection** connection_ptr) {
    if (connection_ptr == NULL) {
        return -1;
    }

    HTTPServerConnection* connection =
        (HTTPServerConnection*)malloc(sizeof(HTTPServerConnection));
    if (connection == NULL) {
        return -2;
    }

    int result = http_server_connection_initiate(connection, fd);
    if (result != 0) {
        free(connection);
        return result;
    }

    *(connection_ptr) = connection;

    return 0;
}

void http_server_connection_set_callback(
    HTTPServerConnection* connection, void* context,
    HttpServerConnectionOnRequest on_request) {
    connection->context   = context;
    connection->onRequest = on_request;
}

int http_server_connection_send(HTTPServerConnection* connection) {
    if (!connection || !connection->write_buffer ||
        connection->write_offset >= connection->write_size) {
        return 0;
    }

    ssize_t sent =
        tcp_client_write(&connection->tcpClient,
                         connection->write_buffer + connection->write_offset,
                         connection->write_size - connection->write_offset);

    if (sent > 0) {
        connection->write_offset += sent;
    } else if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            connection->state = HTTP_SERVER_CONNECTION_STATE_DISPOSE;
            return -1;
        }
    }

    // Finished sending
    if (connection->write_offset >= connection->write_size) {
        connection->state = HTTP_SERVER_CONNECTION_STATE_DISPOSE;
    }

    return 0;
}

// TODO: DIVIDE THIS FN UP INTO SMALLER PIECES FOR EASIER READING ETC
int http_server_connection_receive(HTTPServerConnection* connection) {
    if (!connection) {
        return -1;
    }

    uint8_t chunk_buffer[CHUNK_SIZE];

    int bytes_read = tcp_client_read(&connection->tcpClient, chunk_buffer,
                                     sizeof(chunk_buffer));

    if (bytes_read < 0) {
        return -1; // real error
    } else if (bytes_read == 0) {
        return 0;
    }

    size_t   new_size   = connection->read_buffer_size + bytes_read;
    uint8_t* new_buffer = realloc(connection->read_buffer, new_size);
    if (!new_buffer) {
        return -1;
    }

    connection->read_buffer = new_buffer;
    memcpy(connection->read_buffer + connection->read_buffer_size, chunk_buffer,
           bytes_read);
    connection->read_buffer_size += bytes_read;

    if (connection->body_start == 0) {

        for (int i = 0; i <= connection->read_buffer_size - 4; i++) {

            // Checks if we have parsed all headers
            if (connection->read_buffer[i] == '\r' &&
                connection->read_buffer[i + 1] == '\n' &&
                connection->read_buffer[i + 2] == '\r' &&
                connection->read_buffer[i + 3] == '\n') {

                char   method[METHOD_MAX_LEN]             = {0};
                char   request_path[REQUEST_PATH_MAX_LEN] = {0};
                char   host[HOST_MAX_LEN]                 = {0};
                size_t content_len                        = 0;

                int   header_end = i + 4;
                char* headers    = malloc(header_end + 1);
                if (!headers) {
                    return -1;
                }

                memcpy(headers, connection->read_buffer, header_end);
                headers[header_end] = '\0';

                sscanf(headers, "%7s %255s", method, request_path);

                char* host_ptr = strstr(headers, "Host:");
                if (host_ptr) {
                    sscanf(host_ptr, "Host: %255s", host);
                }

                char* content_len_ptr = strstr(headers, "Content-Length:");
                if (content_len_ptr) {
                    sscanf(content_len_ptr, "Content-Length: %zu",
                           &content_len);
                }

                free(headers);

                connection->method       = strdup(method);
                connection->request_path = strdup(request_path);
                connection->host         = strdup(host);
                connection->content_len  = content_len;
                connection->body_start   = header_end;

                break;
            }
        }
    }

    // checks if headers and body is done parsing
    if (connection->read_buffer_size >=
            connection->body_start + connection->content_len &&
        connection->body_start > 0) {

        if (connection->method && strcmp(connection->method, "GET") == 0) {
            connection->state = HTTP_SERVER_CONNECTION_STATE_SEND;
            connection->onRequest(connection->context);
            return 0;
        }
        connection->body = malloc(connection->content_len);
        if (!connection->body) {
            return -1;
        }

        memcpy(connection->body,
               connection->read_buffer + connection->body_start,
               connection->content_len);

        connection->state = HTTP_SERVER_CONNECTION_STATE_SEND;
        connection->onRequest(connection->context);
    }

    return 0;
}

void http_server_connection_task_work(void* context, uint64_t mon_time) {
    HTTPServerConnection* connection = (HTTPServerConnection*)context;
    switch (connection->state) {
    case HTTP_SERVER_CONNECTION_STATE_RECEIVE:
        http_server_connection_receive(connection);
        break;
    case HTTP_SERVER_CONNECTION_STATE_SEND:
        http_server_connection_send(connection);
        break;
    case HTTP_SERVER_CONNECTION_STATE_DISPOSE:
        http_server_connection_dispose(connection);
        break;
    }
}

void http_server_connection_dispose(HTTPServerConnection* connection) {
    if (!connection) {
        return;
    }

    // Stop and remove the task first
    if (connection->task) {
        smw_destroy_task(connection->task);
        connection->task = NULL;
    }

    // Dispose TCP client
    tcp_client_dispose(&connection->tcpClient);

    // Free all dynamically allocated memory
    free(connection->read_buffer);
    connection->read_buffer = NULL;

    free(connection->body);
    connection->body = NULL;

    free(connection->method);
    connection->method = NULL;

    free(connection->request_path);
    connection->request_path = NULL;

    free(connection->host);
    connection->host = NULL;

    free(connection->write_buffer);
    connection->write_buffer = NULL;

    connection->read_buffer_size = 0;
    connection->write_size       = 0;
    connection->write_offset     = 0;
    connection->body_start       = 0;
    connection->content_len      = 0;
}

void http_server_connection_dispose_ptr(HTTPServerConnection** connection_ptr) {
    if (connection_ptr == NULL || *(connection_ptr) == NULL) {
        return;
    }

    http_server_connection_dispose(*(connection_ptr));
    free(*(connection_ptr));
    *(connection_ptr) = NULL;
}
