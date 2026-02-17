#include "endpoints/citites.h"
#include "endpoints/current.h"
#include "endpoints/echo.h"
#include "endpoints/elpris.h"
#include "endpoints/forecast.h"
#include "endpoints/health.h"
#include "endpoints/home.h"
#include "endpoints/hourly.h"
#include "endpoints/minutely.h"
#include "endpoints/weather.h"

#include <http_utils.h>
#include <stdio.h>

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
    {"GET", "/health", handle_health_check},
    {"GET", "/v1/weather", handle_weather_by_city},
    {"GET", "/v1/current", handle_current_weather},
    {"GET", "/v1/hourly", handle_hourly_weather},
    {"GET", "/v1/minutely", handle_minutely_weather},
    {"GET", "/v1/forecast", handle_forecast_weather},
    {"GET", "/v1/cities", handle_city_search},
    {"GET", "/v1/elpris", handle_elpris_route},
};

#define ROUTE_COUNT (sizeof(g_routes) / sizeof(g_routes[0]))

int handle_not_found(HTTPServerConnection* conn, const char* path) {
    char msg[512];
    snprintf(
        msg, sizeof(msg),
        "The requested endpoint '%s' was not found. Available endpoints: "
        "GET /, POST /echo, GET /v1/health, GET /v1/current?lat=XX&lon=YY, "
        "GET /v1/hourly?lat=XX&lon=YY&hours=24, "
        "GET /v1/minutely?lat=XX&lon=YY&hours=24, GET "
        "/v1/hourly?city=NAME&country=CODE&hours=24, "
        "GET /v1/weather?city=NAME&country=CODE, GET /v1/cities?query=SEARCH",
        path);
    return send_json_error(conn, 404, msg);
}
