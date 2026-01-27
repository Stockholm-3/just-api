/**
 * @file weather_client_smw.c
 * @brief State Machine Worker implementation for weather client
 */

#include "weather_client_smw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* weather_client_get_state_name(request_state_t state) {
    switch (state) {
        case REQ_STATE_IDLE: return "IDLE";
        case REQ_STATE_QUEUED: return "QUEUED";
        case REQ_STATE_CONNECTING: return "CONNECTING";
        case REQ_STATE_SENDING: return "SENDING";
        case REQ_STATE_RECEIVING: return "RECEIVING";
        case REQ_STATE_PROCESSING: return "PROCESSING";
        case REQ_STATE_COMPLETED: return "COMPLETED";
        case REQ_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

int weather_client_smw_work_impl(weather_request_t* requests, int request_count,
                                 uint64_t current_time,
                                 char* (*http_executor)(const char*, int*)) {
    int active = 0;
    
    for (int i = 0; i < request_count; i++) {
        weather_request_t* req = &requests[i];
        if (!req->base_url) continue;
        
        switch (req->state) {
            case REQ_STATE_QUEUED:
                printf("[SMW] Request %d: %s -> CONNECTING\n", i, 
                       weather_client_get_state_name(req->state));
                req->state = REQ_STATE_CONNECTING;
                req->start_time = current_time;
                active++;
                break;
                
            case REQ_STATE_CONNECTING:
                printf("[SMW] Request %d: %s -> SENDING\n", i, 
                       weather_client_get_state_name(req->state));
                req->state = REQ_STATE_SENDING;
                active++;
                break;
                
            case REQ_STATE_SENDING:
                printf("[SMW] Request %d: %s -> RECEIVING\n", i, 
                       weather_client_get_state_name(req->state));
                req->state = REQ_STATE_RECEIVING;
                active++;
                break;
                
            case REQ_STATE_RECEIVING:
                printf("[SMW] Request %d: %s -> PROCESSING\n", i, 
                       weather_client_get_state_name(req->state));
                req->state = REQ_STATE_PROCESSING;
                active++;
                break;
                
            case REQ_STATE_PROCESSING:
                // Actually execute the request
                printf("[SMW] Request %d: Executing HTTP request...\n", i);
                char url[1024];
                snprintf(url, sizeof(url), "%s/%s?%s", 
                        req->base_url, req->endpoint, req->query);
                
                int status_code = 0;
                char* response = http_executor(url, &status_code);
                
                if (req->callback) {
                    req->callback(response, status_code, req->user_data);
                } else if (response) {
                    free(response);
                }
                
                req->state = REQ_STATE_COMPLETED;
                printf("[SMW] Request %d: %s (took %llu ms)\n", i, 
                       weather_client_get_state_name(req->state),
                       (unsigned long long)(current_time - req->start_time));
                break;
                
            case REQ_STATE_COMPLETED:
            case REQ_STATE_ERROR:
                // Keep completed/error requests for one more cycle
                break;
                
            default:
                break;
        }
    }
    
    return active;
}
