/**
 * @file weather_client_smw.h
 * @brief State Machine Worker for async weather client requests
 */

#ifndef WEATHER_CLIENT_SMW_H
#define WEATHER_CLIENT_SMW_H

#include "weather_client.h"

/**
 * @brief State machine worker - process one step of each request
 *
 * Iterates through all queued requests and advances their state machine.
 * Each call processes one state transition per request, allowing for
 * step-by-step async execution with full visibility into the request lifecycle.
 *
 * @param requests Array of weather requests
 * @param request_count Number of requests in the array
 * @param current_time Current time in milliseconds (for timing metrics)
 * @param http_executor Function pointer to execute HTTP requests
 * @return Number of active (non-completed) requests
 */
int weather_client_smw_work_impl(weather_request_t* requests, int request_count,
                                 uint64_t current_time,
                                 char* (*http_executor)(const char*, int*));

/**
 * @brief Get human-readable state name
 * @param state Request state
 * @return State name string
 */
const char* weather_client_get_state_name(request_state_t state);

#endif // WEATHER_CLIENT_SMW_H
