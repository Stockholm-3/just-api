#include "elpris_api.h"

#include <http_client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE_URL "https://www.elprisetjustnu.se/api/v1/prices/"

typedef struct {
    ElprisApiOnResponse user_callback;
    void*               context;
} RequestContext;

static void client_callback(const char* event, const char* response,
                            void* context) {
    RequestContext* ctx = (RequestContext*)context;
    if (!ctx) {
        return;
    }

    if (ctx->user_callback) {
        if (strcmp(event, "RESPONSE") == 0 && response) {
            if (response[0] == '[' || response[0] == '{') {
                ctx->user_callback((char*)response, ctx->context);
            } else {
                printf("Invalid response format: %s\n", response);
                ctx->user_callback(NULL, ctx->context);
            }
        } else {
            printf("Error event: %s - %s\n", event,
                   response ? response : "No response");
            ctx->user_callback(NULL, ctx->context);
        }
    } else {
        printf("No callback registered\n");
    }

    free(ctx);
}

int elpris_api_fetch_async(unsigned int year, unsigned int month,
                           unsigned int day,
                           char price_group[3], // Changed from [3] to [4]
                           ElprisApiOnResponse callback, void* context) {
    if (!callback) {
        return -1;
    }

    RequestContext* ctx = malloc(sizeof(RequestContext));
    if (!ctx) {
        return -1;
    }

    ctx->user_callback = callback;
    ctx->context       = context;

    char url[128];
    snprintf(url, sizeof(url), BASE_URL "%04u/%02u-%02u_%s.json", year, month,
             day, price_group);

    printf("Fetching URL: %s\n", url); // Debug logging

    return http_client_get(url, NULL, 30000, client_callback, ctx);
}

int elpris_api_fetch_query_async(const char*         query,
                                 ElprisApiOnResponse callback, void* context) {
    if (!query || !callback) {
        return -1;
    }

    const char* query_start = query;
    if (query_start[0] == '?') {
        query_start++;
    }

    unsigned int year = 0, month = 0, day = 0;
    char         price_group[4]    = {0};
    int          price_group_found = 0;

    char* query_copy = strdup(query_start);
    if (!query_copy) {
        callback(NULL, context);
        return -1;
    }

    char* token       = strtok(query_copy, "&");
    int   parse_error = 0;

    while (token != NULL && !parse_error) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals     = '\0';
            char* key   = token;
            char* value = equals + 1;

            if (strcmp(key, "date") == 0) {
                if (sscanf(value, "%4u-%2u-%2u", &year, &month, &day) != 3) {
                    parse_error = 1;
                }
            } else if (strcmp(key, "price") == 0) {
                if (strlen(value) >= 3) {
                    strncpy(price_group, value, 3);
                    price_group[3]    = '\0';
                    price_group_found = 1;
                } else if (strlen(value) == 2) {
                    strncpy(price_group, value, 2);
                    price_group[2]    = '\0';
                    price_group_found = 1;
                } else {
                    parse_error = 1;
                }
            }
        }

        token = strtok(NULL, "&");
    }

    free(query_copy);

    if (parse_error || year == 0 || month == 0 || month > 12 || day == 0 ||
        day > 31 || !price_group_found) {
        callback(NULL, context);
        return -1;
    }

    return elpris_api_fetch_async(year, month, day, price_group, callback,
                                  context);
}
