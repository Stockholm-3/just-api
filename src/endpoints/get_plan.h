#include "energy_plan/city_registry.h"
#include "weather_location_parser.h"

#include <ctype.h>
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
        char response[1024];
        snprintf(response, sizeof(response),
                 "City '%s' has just been registered. "
                 "Data will be available within 1 hour.",
                 ctx->city);
        send_json_message(ctx->conn, 202, response);
        break;
    }
    case CITY_OUTPUT_LOCK_ERROR:
        send_json_message(ctx->conn, 503,
                          "Energy output temporarily unavailable; "
                          "could not acquire read lock. Please retry.");
        break;
    case CITY_OUTPUT_READ_ERROR:
        send_json_message(ctx->conn, 500,
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
        send_json_message(ctx->conn, 500,
                          "Internal error initiating output read.");
        free(ctx);
    }
}

int handle_get_plan(HTTPServerConnection* conn, const char* query) {
    if (!query || *query == '\0') {
        return send_json_message(conn, 400, "Missing query parameters");
    }
    UrlQueryMap map;
    if (url_query_parse(query, &map) != 0) {
        return send_json_message(conn, 400, "Invalid query parameters");
    }
    const char* city  = url_query_get(&map, "city");
    const char* price = url_query_get(&map, "price");
    if (!city || !price) {
        return send_json_message(
            conn, 400, "Missing required parameters: city and/or price");
    }
    if (!is_allowed_price(price)) {
        return send_json_message(
            conn, 400,
            "Invalid price parameter; must be SE1, SE2, SE3, or SE4");
    }

    char city_normalized[256];
    strncpy(city_normalized, city, sizeof(city_normalized) - 1);
    city_normalized[sizeof(city_normalized) - 1] = '\0';
    for (int i = 0; city_normalized[i]; i++) {
        city_normalized[i] = (char)tolower((unsigned char)city_normalized[i]);
    }
    if (city_normalized[0]) {
        city_normalized[0] = (char)toupper((unsigned char)city_normalized[0]);
    }

    Coordinates coords = get_city_coordinates(
        "data/swedish_cities_locations.csv", city_normalized);
    if (!coords.found) {
        char err[328];
        snprintf(err, sizeof(err), "City not found: %s", city_normalized);
        return send_json_message(conn, 400, err);
    }

    PlanRequestContext* ctx = malloc(sizeof(PlanRequestContext));
    if (!ctx) {
        return send_json_message(conn, 500, "Internal error");
    }
    ctx->conn = conn;
    strncpy(ctx->city, city_normalized, sizeof(ctx->city) - 1);
    ctx->city[sizeof(ctx->city) - 1] = '\0';
    strncpy(ctx->price, price, sizeof(ctx->price) - 1);
    ctx->price[sizeof(ctx->price) - 1] = '\0';

    if (city_registry_initiate(CITY_REGISTRY_FILE, city_normalized, price,
                               coords.lat, coords.lon, ctx,
                               on_registry_done) != 0) {
        free(ctx);
        return send_json_message(conn, 500, "Internal error");
    }
    return 0;
}
