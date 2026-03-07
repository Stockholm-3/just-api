/**
 * @file get_plan.c
 * @brief HTTP handler for GET /plan?city=…&price=…
 *
 * Registers (or refreshes) the city in the energy-plan store, then
 * asynchronously reads the latest compute output and returns it to
 * the client. All I/O goes through energy_plan_store.
 */

#include "energy_plan/energy_plan_store.h"
#include "weather_location_parser.h"

#include <ctype.h>
#include <http_utils.h>
#include <stdlib.h>
#include <string.h>
#include <url_query_parser.h>

static const char* const ALLOWED_ZONES[] = {"SE1", "SE2", "SE3", "SE4"};
#define NUM_ALLOWED_ZONES                                                      \
    (int)(sizeof(ALLOWED_ZONES) / sizeof(ALLOWED_ZONES[0]))

static int is_valid_zone(const char* value) {
    if (!value) {
        return 0;
    }
    for (int i = 0; i < NUM_ALLOWED_ZONES; i++) {
        if (strcmp(value, ALLOWED_ZONES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

typedef struct {
    HTTPServerConnection* conn;
    char                  city[256];
    char                  price[16];
} PlanCtx;

static void on_output_read(void* context, EpOutputStatus status, char* buf,
                           size_t len) {
    PlanCtx* ctx = (PlanCtx*)context;

    switch (status) {
    case EP_OUTPUT_OK:
        send_response(ctx->conn, 200, "application/json", buf, len);
        free(buf);
        break;

    case EP_OUTPUT_NOT_FOUND: {
        char msg[320];
        snprintf(msg, sizeof(msg),
                 "City '%s' has just been registered. "
                 "Data will be available within 1 hour.",
                 ctx->city);
        send_json_message(ctx->conn, 202, msg);
        break;
    }

    case EP_OUTPUT_LOCK_ERROR:
        send_json_message(ctx->conn, 503,
                          "Energy output temporarily unavailable; "
                          "could not acquire read lock. Please retry.");
        break;

    case EP_OUTPUT_READ_ERROR:
        send_json_message(ctx->conn, 500,
                          "Internal error reading compute output.");
        break;
    }

    free(ctx);
}

static void on_city_registered(void* context, EpCityRegisterStatus status) {
    (void)status; /* ADDED, EXISTS, and LIMIT_REACHED all proceed to read */

    PlanCtx* ctx = (PlanCtx*)context;

    if (energy_plan_store_read_output_async(ctx->city, ctx->price, ctx,
                                            on_output_read) != 0) {
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
    if (!is_valid_zone(price)) {
        return send_json_message(conn, 400,
                                 "Invalid price parameter; "
                                 "must be SE1, SE2, SE3, or SE4");
    }

    // Normalise city name: all lower, then capitalise first letter beacuse why
    // not.
    char city_norm[256];
    strncpy(city_norm, city, sizeof(city_norm) - 1);
    city_norm[sizeof(city_norm) - 1] = '\0';
    for (int i = 0; city_norm[i]; i++) {
        city_norm[i] = (char)tolower((unsigned char)city_norm[i]);
    }
    if (city_norm[0]) {
        city_norm[0] = (char)toupper((unsigned char)city_norm[0]);
    }

    Coordinates coords =
        get_city_coordinates("data/swedish_cities_locations.csv", city_norm);
    if (!coords.found) {
        char err[320];
        snprintf(err, sizeof(err), "City not found: %s", city_norm);
        return send_json_message(conn, 400, err);
    }

    PlanCtx* ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return send_json_message(conn, 500, "Internal error");
    }

    ctx->conn = conn;
    strncpy(ctx->city, city_norm, sizeof(ctx->city) - 1);
    strncpy(ctx->price, price, sizeof(ctx->price) - 1);
    ctx->city[sizeof(ctx->city) - 1]   = '\0';
    ctx->price[sizeof(ctx->price) - 1] = '\0';

    if (energy_plan_store_register_city(city_norm, price, coords.lat,
                                        coords.lon, ctx,
                                        on_city_registered) != 0) {
        free(ctx);
        return send_json_message(conn, 500, "Internal error");
    }

    return 0;
}
