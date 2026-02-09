/**
 * geocoding_http.c - Implementation för HTTP och URL-hjälp för geokodning
 */

#include <geocoding_http.h>
#include <http_client.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Återanvänd samma API-URL som i geocoding_api.c */
#ifndef GEOCODING_API_URL
#    define GEOCODING_API_URL "http://geocoding-api.open-meteo.com/v1/search"
#endif

typedef struct {
    char*        response_data;
    size_t       response_size;
    int          http_status;
    volatile int completed;
    volatile int error;
} HttpFetchContext;

static HttpFetchContext* g_fetch_context = NULL;

/* HTTP-callback som tar emot svar eller fel och signalerar färdigställande */
static void http_fetch_callback(const char* event, const char* response,
                                void* context) {
    if (!g_fetch_context) {
        return;
    }

    if (strcmp(event, "RESPONSE") == 0) {
        g_fetch_context->response_data = strdup(response);
        g_fetch_context->response_size = strlen(response);
        g_fetch_context->http_status   = 200;
        g_fetch_context->completed     = 1;
    } else if (strcmp(event, "ERROR") == 0 || strcmp(event, "TIMEOUT") == 0) {
        g_fetch_context->error     = 1;
        g_fetch_context->completed = 1;
    }
}

/* Enkel URL-encoding för query-parametrar */
static int url_encode_char(const char* src, int src_len, char* dst,
                           int dst_size) {
    int dst_pos = 0;
    for (int i = 0; i < src_len && dst_pos + 3 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            dst[dst_pos++] = c;
        } else if (c == ' ') {
            dst[dst_pos++] = '+';
        } else {
            if (dst_pos + 2 >= dst_size) {
                break;
            }
            dst_pos += snprintf(dst + dst_pos, dst_size - dst_pos, "%%%02X", c);
        }
    }
    dst[dst_pos] = '\0';
    return dst_pos;
}

/* Bygger URL för API-anropet */
char* geocoding_build_api_url(const char* city_name, const char* country,
                              int max_results, const char* language) {
    char* url = malloc(2048);
    if (!url) {
        return NULL;
    }

    /* Koda stadsnamnet */
    char encoded_city[512];
    url_encode_char(city_name, strlen(city_name), encoded_city,
                    sizeof(encoded_city));

    int written =
        snprintf(url, 2048, "%s?name=%s&count=%d&language=%s&format=json",
                 GEOCODING_API_URL, encoded_city, max_results, language);

    if (country) {
        /* Koda land om det anges */
        char encoded_country[256];
        url_encode_char(country, strlen(country), encoded_country,
                        sizeof(encoded_country));
        written += snprintf(url + written, 2048 - written, "&country=%s",
                            encoded_country);
    }

    return url;
}

/* Synkron HTTP-hämtning som väntar på callback via event-loop */
int geocoding_fetch_url_sync(const char* url, char** response_data,
                             int* http_status) {
    HttpFetchContext context = {0};
    g_fetch_context          = &context;

    http_client_get(url, NULL, 30000, http_fetch_callback, NULL);

    /* Polla event-loopen utan sleep */
    time_t start_time      = time(NULL);
    time_t timeout_seconds = 30;

    while (!context.completed) {
        smw_work(0); /* Pass 0 om monotont tid inte finns */

        /* Kontrollera timeout (1-sekunds upplösning) */
        if (time(NULL) - start_time > timeout_seconds) {
            fprintf(stderr, "[GEOCODING] Timeout waiting for response\n");
            break;
        }
    }

    g_fetch_context = NULL;

    if (context.error || !context.completed) {
        if (context.response_data) {
            free(context.response_data);
        }
        return -1;
    }

    *response_data = context.response_data;
    *http_status   = context.http_status;

    return 0;
}
