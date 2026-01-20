#include "open_meteo_handler.h"
#include "weather_location_handler.h"

#include <http_utils.h>

int handle_homepage(HTTPServerConnection* conn, const char* query);
int handle_echo(HTTPServerConnection* conn, const char* query);
int handle_weather_by_city(HTTPServerConnection* conn, const char* query);
int handle_current_weather(HTTPServerConnection* conn, const char* query);
int handle_city_search(HTTPServerConnection* conn, const char* query);
int handle_not_found(HTTPServerConnection* conn, const char* path);

// -------------------------
// Routing table
// -------------------------

typedef int (*RouteHandler)(HTTPServerConnection*, const char*);

typedef struct {
    const char*  method;
    const char*  path;
    RouteHandler handler;
} Route;

Route g_routes[] = {
    {"GET", "/", handle_homepage},
    {"GET", "/echo", handle_echo},
    {"POST", "/echo", handle_echo},
    {"GET", "/v1/weather", handle_weather_by_city},
    {"GET", "/v1/current", handle_current_weather},
    {"GET", "/v1/cities", handle_city_search},
};

#define ROUTE_COUNT (sizeof(g_routes) / sizeof(g_routes[0]))

int handle_homepage(HTTPServerConnection* conn, const char* query) {
    const char* html = "<!DOCTYPE html>"
                       "<html>"
                       "<head><title>Just Weather</title></head>"
                       "<body>"
                       "<h1>Just Weather API</h1>"
                       "<p>Available endpoints:</p>"
                       "<ul>"
                       "  <li><b>GET /echo</b> - echo raw request</li>"
                       "  <li><b>POST /echo</b> - echo raw body</li>"
                       "  <li><b>GET /v1/current?lat=XX&lon=YY</b> - current "
                       "weather by coordinates</li>"
                       "  <li><b>GET /v1/weather?city=NAME&country=CODE</b> - "
                       "weather by city name</li>"
                       "  <li><b>GET /v1/cities?query=SEARCH</b> - city search "
                       "(autocomplete)</li>"
                       "</ul>"
                       "<p>Source code on <a "
                       "href=\"https://github.com/Stockholm-3/"
                       "just-weather-server\" target=\"_blank\">GitHub</a>.</p>"
                       "</body>"
                       "</html>";

    return send_response(conn, 200, "text/html", html, strlen(html));
}

int handle_echo(HTTPServerConnection* conn, const char* query) {
    size_t body_len = conn->read_buffer_size;
    return send_response(conn, 200, "text/plain", (char*)conn->read_buffer,
                         body_len);
}

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

int handle_city_search(HTTPServerConnection* conn, const char* query) {
    char* json_response = NULL;
    int   status_code   = 0;

    weather_location_handler_search_cities(query, &json_response, &status_code);

    if (!json_response) {
        return send_json_error(conn, 500, "Failed to search cities");
    }

    int ret = send_response(conn, status_code, "application/json",
                            json_response, strlen(json_response));
    free(json_response);
    return ret;
}

int handle_not_found(HTTPServerConnection* conn, const char* path) {
    char msg[512];
    snprintf(msg, sizeof(msg),
             "The requested endpoint '%s' was not found. Available endpoints: "
             "GET /, POST /echo, GET /v1/current?lat=XX&lon=YY, GET "
             "/v1/weather?city=NAME&country=CODE, GET /v1/cities?query=SEARCH",
             path);
    return send_json_error(conn, 404, msg);
}
