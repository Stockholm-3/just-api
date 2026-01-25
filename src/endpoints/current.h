#include "open_meteo_handler.h"

#include <http_utils.h>
#include <stdlib.h>
#include <string.h>

int handle_current_weather(HTTPServerConnection* conn, const char* query) {
    char* json_response = NULL;
    int   status_code   = 0;

    open_meteo_handler_current(query, &json_response, &status_code);

    if (!json_response) {
        return send_json_error(
            conn, 500, "Failed to fetch weather data from Open-Meteo API");
    }

    int ret = send_response(conn, status_code, "application/json",
                            json_response, strlen(json_response));
    free(json_response);
    return ret;
}
