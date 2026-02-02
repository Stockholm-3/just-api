#include "weather_location_handler.h"

#include <http_utils.h>
#include <stdlib.h>
#include <string.h>

int handle_weather_by_city(HTTPServerConnection* conn, const char* query) {
    return weather_location_handler_by_city_async(conn, query);
}
