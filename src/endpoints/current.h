#include "open_meteo_handler.h"

#include <http_utils.h>
#include <stdlib.h>
#include <string.h>

int handle_current_weather(HTTPServerConnection* conn, const char* query) {
    return open_meteo_handler_current_async(conn, query);
}
