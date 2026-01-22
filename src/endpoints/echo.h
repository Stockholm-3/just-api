#include <http_utils.h>

int handle_echo(HTTPServerConnection* conn, const char* query) {
    size_t body_len = conn->read_buffer_size;
    return send_response(conn, 200, "text/plain", (char*)conn->read_buffer,
                         body_len);
}
