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

    if (strcmp(event, "RESPONSE") == 0) {
        if (ctx->user_callback && response) {
            ctx->user_callback((char*)response, ctx->context);
        }
    } else if (strcmp(event, "ERROR") == 0 || strcmp(event, "TIMEOUT") == 0) {
        if (ctx->user_callback && response) {
            ctx->user_callback((char*)response, ctx->context);
        }
        printf("failed to fetch. event:%s Reason:%s", event, response);
    }

    free(ctx);
}

int elpris_api_fetch_async(unsigned int year, unsigned int month,
                           unsigned int day, ElprisApiOnResponse callback,
                           void* context) {
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
    snprintf(url, sizeof(url), BASE_URL "%04u/%02u-%02u_SE3.json", year, month,
             day);

    return http_client_get(url, NULL, 30000, client_callback, ctx);
}
