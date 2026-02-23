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
} PlanRequestContext;
static void on_output_read(void* context, CityOutputStatus status, char* buf,
                           size_t len) {
    PlanRequestContext* ctx = (PlanRequestContext*)context;
    switch (status) {
    case CITY_OUTPUT_OK:
        send_response(ctx->conn, 200, "application/json", buf, len);
        free(buf);
        break;
    case CITY_OUTPUT_NOT_FOUND: {
        char err[1024];
        snprintf(err, sizeof(err),
                 "Compute output not yet available for city '%s'"
                 "City '%s' is now registered and the data will be availiable "
                 "within 1 hour",
                 ctx->city, ctx->city);
        send_json_error(ctx->conn, 503, err);
        break;
    }
    case CITY_OUTPUT_LOCK_ERROR:
        send_json_error(ctx->conn, 503,
                        "Energy output temporarily unavailable; "
                        "could not acquire read lock. Please retry.");
        break;
    case CITY_OUTPUT_READ_ERROR:
        send_json_error(ctx->conn, 500,
                        "Internal error reading compute output.");
        break;
    }
    free(ctx);
}
static void on_registry_done(void* context, CityRegisterStatus status) {
    (void)status;
    PlanRequestContext* ctx = (PlanRequestContext*)context;
    if (city_output_read_initiate(ctx->city, ctx->price, ctx, on_output_read) !=
        0) {
        send_json_error(ctx->conn, 500,
                        "Internal error initiating output read.");
        free(ctx);
    }
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
    strncpy(ctx->city, city, sizeof(ctx->city) - 1);
    ctx->city[sizeof(ctx->city) - 1] = '\0';
    strncpy(ctx->price, price, sizeof(ctx->price) - 1);
    ctx->price[sizeof(ctx->price) - 1] = '\0';
    if (city_registry_initiate(CITY_REGISTRY_FILE, city, price, coords.lat,
                               coords.lon, ctx, on_registry_done) != 0) {
        free(ctx);
        return send_json_error(conn, 500, "Internal error");
    }
    return 0;
}
