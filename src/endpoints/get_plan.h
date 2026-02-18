#include "energy_plan/city_registry.h"
#include "weather_location_parser.h"

#include <http_utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <url_query_parser.h>

static const char* ALLOWED_PRICE_VALUES[] = {"SE1", "SE2", "SE3", "SE4"};
#define NUM_ALLOWED_PRICES                                                     \
    (sizeof(ALLOWED_PRICE_VALUES) / sizeof(ALLOWED_PRICE_VALUES[0]))

static int is_allowed_price(const char* value) {
    if (!value) {
        return 0;
    }
    for (size_t i = 0; i < NUM_ALLOWED_PRICES; i++) {
        if (strcmp(value, ALLOWED_PRICE_VALUES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

typedef struct {
    HTTPServerConnection* conn;
    char                  city[256];
    char                  price[16];
    double                lat;
    double                lon;
} PlanRequestContext;

static void on_registry_done(void* context, CityRegisterStatus status) {
    PlanRequestContext* ctx = (PlanRequestContext*)context;

    const char* status_msg = (status == CITY_ADDED) ? "City has been added"
                             : (status == CITY_EXISTS)
                                 ? "City already exists (timestamp updated)"
                                 : "City not added; maximum limit reached";

    char response[1024];
    int  written =
        snprintf(response, sizeof(response),
                 "{ \"city\": \"%s\", \"price\": \"%s\", \"lat\": "
                 "%.6f, \"lon\": %.6f, \"status\": \"%s\" }",
                 ctx->city, ctx->price, ctx->lat, ctx->lon, status_msg);

    if (written < 0 || written >= (int)sizeof(response)) {
        send_json_error(ctx->conn, 500, "Response too large");
    } else {
        send_response(ctx->conn, 200, "application/json", response,
                      (size_t)written);
    }

    free(ctx);
}

int handle_get_plan(HTTPServerConnection* conn, const char* query) {
    if (!query || *query == '\0') {
        return send_json_error(conn, 400, "Missing query parameters");
    }

    UrlQueryMap map;
    if (url_query_parse(query, &map) != 0) {
        return send_json_error(conn, 400, "Invalid query parameters");
    }

    const char* city  = url_query_get(&map, "city");
    const char* price = url_query_get(&map, "price");

    if (!city || !price) {
        return send_json_error(
            conn, 400, "Missing required parameters: city and/or price");
    }

    if (!is_allowed_price(price)) {
        return send_json_error(
            conn, 400,
            "Invalid price parameter; must be SE1, SE2, SE3, or SE4");
    }

    Coordinates coords =
        get_city_coordinates("data/swedish_cities_locations.csv", city);
    if (!coords.found) {
        char err[256];
        snprintf(err, sizeof(err), "City not found: %s", city);
        return send_json_error(conn, 400, err);
    }

    PlanRequestContext* ctx = malloc(sizeof(PlanRequestContext));
    if (!ctx) {
        return send_json_error(conn, 500, "Internal error");
    }

    ctx->conn = conn;
    strncpy(ctx->city, city, sizeof(ctx->city));
    ctx->city[sizeof(ctx->city) - 1] = '\0';
    strncpy(ctx->price, price, sizeof(ctx->price));
    ctx->price[sizeof(ctx->price) - 1] = '\0';
    ctx->lat                           = coords.lat;
    ctx->lon                           = coords.lon;

    if (city_registry_initiate(CITY_REGISTRY_FILE, city, price, coords.lat,
                               coords.lon, ctx, on_registry_done) != 0) {
        free(ctx);
        return send_json_error(conn, 500, "Internal error");
    }

    return 0;
}
