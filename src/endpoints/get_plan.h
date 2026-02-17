#include <http_utils.h>
#include <stdio.h>
#include <string.h>

int handle_get_plan(HTTPServerConnection* conn, const char* query) {
    if (query == NULL || strlen(query) == 0) {
        return send_json_error(conn, 400, "Missing query parameters");
    }

    char city[256]  = {0};
    char price[256] = {0};

    char query_copy[512];
    strncpy(query_copy, query, sizeof(query_copy) - 1);
    query_copy[sizeof(query_copy) - 1] = '\0';

    char* token = strtok(query_copy, "&");

    while (token != NULL) {
        if (strncmp(token, "city=", 5) == 0) {
            strncpy(city, token + 5, sizeof(city) - 1);
            city[sizeof(city) - 1] = '\0';
        } else if (strncmp(token, "price=", 6) == 0) {
            strncpy(price, token + 6, sizeof(price) - 1);
            price[sizeof(price) - 1] = '\0';
        }

        token = strtok(NULL, "&");
    }

    if (city[0] == '\0' || price[0] == '\0') {
        return send_json_error(conn, 400,
                               "Missing required parameters: city and price");
    }

    char response[700];

    int written =
        snprintf(response, sizeof(response),
                 "{ \"city\": \"%s\", \"price\": \"%s\" }", city, price);

    /* Detect truncation */
    if (written < 0 || written >= (int)sizeof(response)) {
        return send_json_error(conn, 500, "Response too large");
    }

    return send_response(conn, 200, "application/json", response,
                         (size_t)written);
}
