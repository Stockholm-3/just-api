#include "elpris_api.h"

#include <http_client.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GEOCODING_API_URL "https://www.elprisetjustnu.se/api/v1/prices/"
#define DEFAULT_CACHE_DIR "./cache/elpris_cache"
#define DEFAULT_CACHE_TTL 604800 /* 7 days */
#define DEFAULT_LANGUAGE "eng"

typedef struct {
    char*        response_data;
    size_t       response_size;
    int          http_status;
    volatile int completed;
    volatile int error;
} HttpFetchContext;

static HttpFetchContext* g_fetch_context = NULL;

static void http_fetch_callback(const char* event, const char* response) {
    HttpFetchContext* ctx = g_fetch_context;
    if (!ctx) {
        return;
    }

    if (strcmp(event, "RESPONSE") == 0) {
        if (!response) {
            ctx->error = 1;
        } else {
            ctx->response_data = strdup(response);
            ctx->response_size = strlen(response);
            ctx->http_status   = 200;
        }
        ctx->completed = 1;
    } else if (strcmp(event, "ERROR") == 0 || strcmp(event, "TIMEOUT") == 0) {
        ctx->error     = 1;
        ctx->completed = 1;
    }
}

int fetch_url_sync(const char* url, char** response_data, int* http_status) {
    HttpFetchContext* context = calloc(1, sizeof(*context));
    if (!context) {
        return -1;
    }

    g_fetch_context = context;

    http_client_get(url, 30000, http_fetch_callback, "80");

    time_t start_time      = time(NULL);
    time_t timeout_seconds = 30;

    while (!context->completed) {
        smw_work(0);

        if (time(NULL) - start_time > timeout_seconds) {
            fprintf(stderr, "[ELPRIS] Timeout waiting for response\n");
            context->error     = 1;
            context->completed = 1; // 🔴 REQUIRED
            break;
        }
    }

    g_fetch_context = NULL;

    if (context->error || !context->response_data) {
        free(context->response_data);
        free(context);
        return -1;
    }

    *response_data = context->response_data;
    *http_status   = context->http_status;

    free(context); // caller now owns response_data
    return 0;
}
