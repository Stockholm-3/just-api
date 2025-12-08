#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "smw.h"
#include "tcp_client.h"

#ifndef http_client_max_url_length
#    define http_client_max_url_length 1024
#endif

typedef enum {
    HTTP_CLIENT_STATE_INIT       = 0,
    HTTP_CLIENT_STATE_CONNECT    = 1,
    HTTP_CLIENT_STATE_CONNECTING = 2,
    HTTP_CLIENT_STATE_WRITING    = 3,
    HTTP_CLIENT_STATE_READING    = 4, // kanske lägger till connecting
    HTTP_CLIENT_STATE_DONE       = 5,
    HTTP_CLIENT_STATE_DISPOSE    = 6,

} HttpClientState;

// TODO:change to send response with heap instead of copyuting to
// stack!!!!!!!!!!!!!!!!!!!
typedef struct {
    HttpClientState state;
    SmwTask*        task;
    char            url[http_client_max_url_length + 1];
    uint64_t        timeout;

    void (*callback)(const char* event, const char* response);

    uint64_t timer;

    uint8_t* write_buffer;
    size_t   write_size;
    size_t   write_offset;

    uint8_t* read_buffer;      // Buffer for incoming data
    size_t   read_buffer_size; // Current size of read buffer
    size_t   body_start;       // Position where HTTP body starts
    size_t   content_len;      // Content-Length from headers
    int      status_code;      // HTTP status code (200, 404, etc.)
    uint8_t* body;             // Extracted response body

    /* HTTP transfer encoding support */
    int chunked;          // Transfer-Encoding: chunked detected
    int connection_close; // Connection: close header present

    TCPClient*
         tcp_conn;      // Handle to TCP connection, är en tcp connection struct
    char hostname[256]; // Parsed from URL
    char path[512];     // Parsed from URL
    char port[16];      // Parsed from URL
    char response[8192];
} HttpClient;

HttpClientState http_client_work_init(HttpClient* client);
HttpClientState http_client_work_connect(HttpClient* client);
HttpClientState http_client_work_writing(HttpClient* client);
HttpClientState
http_client_work_reading(HttpClient* client); // THIS WAS MISSING
HttpClientState http_client_work_done(HttpClient* client);

int http_client_get(const char* url, uint64_t timeout,
                    void (*callback)(const char* event, const char* response),
                    const char* port);

#endif // http_client_h
