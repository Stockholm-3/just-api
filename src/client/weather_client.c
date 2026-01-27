/**
 * @file weather_client.c
 * @brief Minimal async weather API client implementation
 */

#include "weather_client.h"
#include "weather_client_smw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <strings.h>

#define MAX_REQUESTS 16
#define BUFFER_SIZE 8192

static char g_base_url[256] = {0};
static weather_request_t g_requests[MAX_REQUESTS] = {0};
static int g_request_count = 0;

// Simple HTTP GET request
static char* http_get_sync(const char* url, int* status_code) {
    char host[256], path[512];
    int port = 80;
    
    // Parse URL (simple http://host:port/path parser)
    if (sscanf(url, "http://%255[^:/]:%d/%511[^\n]", host, &port, path) < 2) {
        if (sscanf(url, "http://%255[^/]/%511[^\n]", host, path) < 1) {
            return NULL;
        }
    }
    
    // Resolve host
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return NULL;
    }
    
    // Connect
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0 || connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        if (sock >= 0) close(sock);
        return NULL;
    }
    freeaddrinfo(res);
    
    // Send HTTP request
    char request[1024];
    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n\r\n",
             path, host);
    
    send(sock, request, strlen(request), 0);
    
    // Read response
    char* response = malloc(BUFFER_SIZE);
    if (!response) {
        close(sock);
        return NULL;
    }
    
    int total = 0;
    int n;
    while ((n = recv(sock, response + total, BUFFER_SIZE - total - 1, 0)) > 0) {
        total += n;
        if (total >= BUFFER_SIZE - 1) break;
    }
    response[total] = '\0';
    close(sock);
    
    // Parse status code
    if (sscanf(response, "HTTP/1.1 %d", status_code) != 1) {
        *status_code = 500;
    }
    
    // Find JSON body (after \r\n\r\n)
    char* body = strstr(response, "\r\n\r\n");
    if (body) {
        body += 4;
        char* json = strdup(body);
        free(response);
        return json;
    }
    
    free(response);
    return NULL;
}

int weather_client_init(const char* base_url) {
    if (!base_url) return -1;
    strncpy(g_base_url, base_url, sizeof(g_base_url) - 1);
    g_request_count = 0;
    return 0;
}

int weather_client_current_async(const char* city, const char* country_code,
                                  weather_callback_t callback, void* user_data) {
    if (g_request_count >= MAX_REQUESTS) return -1;
    
    weather_request_t* req = &g_requests[g_request_count++];
    req->base_url = strdup(g_base_url);
    req->endpoint = strdup("weather");
    
    char query[256];
    snprintf(query, sizeof(query), "city=%s&country=%s&current=true", city, country_code);
    req->query = strdup(query);
    req->callback = callback;
    req->user_data = user_data;
    req->state = REQ_STATE_QUEUED;
    req->start_time = 0;
    
    return 0;
}

int weather_client_forecast_async(const char* city, const char* country_code, int days,
                                   weather_callback_t callback, void* user_data) {
    if (g_request_count >= MAX_REQUESTS) return -1;
    
    weather_request_t* req = &g_requests[g_request_count++];
    req->base_url = strdup(g_base_url);
    req->endpoint = strdup("weather");
    
    char query[256];
    snprintf(query, sizeof(query), "city=%s&country=%s&forecast=true&days=%d", city, country_code, days);
    req->query = strdup(query);
    req->callback = callback;
    req->user_data = user_data;
    req->state = REQ_STATE_QUEUED;
    req->start_time = 0;
    
    return 0;
}

// Forward declaration for SMW
static char* http_get_sync(const char* url, int* status_code);

int weather_client_smw_work(uint64_t current_time) {
    return weather_client_smw_work_impl(g_requests, g_request_count, 
                                        current_time, http_get_sync);
}

int weather_client_poll(void) {
    int processed = 0;
    
    for (int i = 0; i < g_request_count; i++) {
        weather_request_t* req = &g_requests[i];
        if (!req->base_url) continue;
        
        // Build full URL
        char url[1024];
        snprintf(url, sizeof(url), "%s/%s?%s", req->base_url, req->endpoint, req->query);
        
        // Execute request
        int status_code = 0;
        char* response = http_get_sync(url, &status_code);
        
        // Call callback
        if (req->callback) {
            req->callback(response, status_code, req->user_data);
        } else if (response) {
            free(response);
        }
        
        // Cleanup
        free(req->base_url);
        free(req->endpoint);
        free(req->query);
        req->base_url = NULL;
        
        processed++;
    }
    
    g_request_count = 0;
    return processed;
}

void weather_client_cleanup(void) {
    for (int i = 0; i < g_request_count; i++) {
        if (g_requests[i].base_url) {
            free(g_requests[i].base_url);
            free(g_requests[i].endpoint);
            free(g_requests[i].query);
        }
    }
    g_request_count = 0;
}

#ifdef WEATHER_CLIENT_MAIN

// Callback for current weather
void on_current_weather(char* response, int status_code, void* user_data) {
    const char* city = (const char*)user_data;
    printf("\n=== Current Weather for %s ===\n", city);
    printf("Status: %d\n", status_code);
    if (response) {
        printf("%s\n", response);
        free(response);
    }
}

// Callback for forecast
void on_forecast(char* response, int status_code, void* user_data) {
    const char* city = (const char*)user_data;
    printf("\n=== 7-Day Forecast for %s ===\n", city);
    
    if (status_code == 200 && response) {
        // Extract temperature for simulated forecast
        char* temp_pos = strstr(response, "\"temperature\":");
        if (temp_pos) {
            double temp;
            if (sscanf(temp_pos, "\"temperature\": %lf", &temp) == 1) {
                printf("7-Day Temperature Forecast:\n");
                const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
                for (int day = 0; day < 7; day++) {
                    // Simulate realistic variations
                    double day_temp = temp + (day - 3) * 0.5;
                    printf("  %s: %.1f°C\n", days[day], day_temp);
                }
                printf("\nSimulated placeholder for a 7-Day temperature forecast until server implements forecast endpoint.\n");
            }
        }
        free(response);
    } else {
        printf("Status: %d\n", status_code);
        if (response) {
            printf("%s\n", response);
            free(response);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: %s <base_url> <city> <country_code> [--smw]\n", argv[0]);
        printf("Example: %s http://localhost:10680/v1 Stockholm SE\n", argv[0]);
        printf("  --smw  Use state machine worker mode\n");
        return 1;
    }
    
    const char* base_url = argv[1];
    const char* city = argv[2];
    const char* country_code = argv[3];
    int use_smw = (argc > 4 && strcmp(argv[4], "--smw") == 0);
    
    printf("Weather Client Demo\n");
    printf("===================\n");
    printf("Base URL: %s\n", base_url);
    printf("Location: %s, %s\n\n", city, country_code);
    
    // Initialize client
    if (weather_client_init(base_url) != 0) {
        fprintf(stderr, "Failed to initialize client\n");
        return 1;
    }
    
    // Queue async requests
    printf("Queueing async requests...\n");
    
    weather_client_current_async(city, country_code, on_current_weather, (void*)city);
    weather_client_forecast_async(city, country_code, 7, on_forecast, (void*)city);
    
    if (use_smw) {
        // State machine worker mode
        printf("\nProcessing with State Machine Worker...\n");
        printf("==========================================\n");
        
        uint64_t time_ms = 0;
        int max_iterations = 20;
        
        for (int iter = 0; iter < max_iterations; iter++) {
            int active = weather_client_smw_work(time_ms);
            time_ms += 10; // Simulate 10ms per iteration
            
            if (active == 0 && iter > 5) {
                printf("\n[SMW] All requests completed\n");
                break;
            }
        }
    } else {
        // Direct poll mode
        printf("Processing requests...\n");
        int processed = weather_client_poll();
        printf("\nProcessed %d requests\n", processed);
    }
    
    // Cleanup
    weather_client_cleanup();
    
    return 0;
}

#endif // WEATHER_CLIENT_MAIN
