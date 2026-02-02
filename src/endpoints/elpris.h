#include "api/elpris/elpris_api.h"

int handle_elpris_route(HTTPServerConnection* conn, const char* query) {
    return elpris_api_fetch_and_respond(conn, query);
}
