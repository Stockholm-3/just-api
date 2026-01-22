#include "weather_location_handler.h"

#include <http_utils.h>

int handle_weather_by_city(HTTPServerConnection* conn, const char* query) {
    char* json_response = NULL;
    int   status_code   = 0;

    weather_location_handler_by_city(query, &json_response, &status_code);

    if (!json_response) {
        return send_json_error(conn, 500,
                               "Failed to fetch weather data for city");
    }

    int ret = send_response(conn, status_code, "application/json",
                            json_response, strlen(json_response));
    free(json_response);
    return ret;
}
