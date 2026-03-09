#ifndef HEALTH_H
#define HEALTH_H

#include <http_utils.h>
#include <string.h>

int handle_health_check(HTTPServerConnection* conn, const char* query) {
    (void)query;
    const char* response = "{\"status\":\"ok\"}\n";
    return send_response(conn, 200, "application/json", (char*)response,
                         strlen(response));
}

#endif
