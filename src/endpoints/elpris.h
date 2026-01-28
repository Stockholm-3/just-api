// In your routes file
#include "api/elpris/elpris_api.h"

#include <http_server_connection.h>
#include <http_utils.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    HTTPServerConnection* conn;
} ElprisRouteContext;

// Callback for the router
static int elpris_route_callback(char* json_data, void* ctx) {
    ElprisRouteContext* context = (ElprisRouteContext*)ctx;
    if (context && context->conn) {
        send_response(context->conn, 200, "application/json", json_data,
                      strlen(json_data));
    }
    free(context);
    return 0;
}

// Router handler
int handle_elpris_route(HTTPServerConnection* conn, const char* query) {
    ElprisRouteContext* ctx = malloc(sizeof(ElprisRouteContext));
    if (!ctx) {
        return -1;
    }

    ctx->conn = conn;

    // Example fixed date, could parse from query parameters
    return elpris_api_fetch_async(2026, 1, 28, elpris_route_callback, ctx);
}
