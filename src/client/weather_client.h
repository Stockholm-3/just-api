/**
 * @file weather_client.h
 * @brief Async Weather API Client
 *
 * Minimal async C client for weather API with forecast support.
 */

#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Callback function type for async weather responses
 * @param response JSON response string (caller must free)
 * @param status_code HTTP status code
 * @param user_data User-provided context data
 */
typedef void (*weather_callback_t)(char* response, int status_code,
                                   void* user_data);

/**
 * @brief Request state for state machine worker
 */
typedef enum {
    REQ_STATE_IDLE = 0,
    REQ_STATE_QUEUED,
    REQ_STATE_CONNECTING,
    REQ_STATE_SENDING,
    REQ_STATE_RECEIVING,
    REQ_STATE_PROCESSING,
    REQ_STATE_COMPLETED,
    REQ_STATE_ERROR
} request_state_t;

/**
 * @brief Async weather request context
 */
typedef struct {
    char*              base_url;
    char*              endpoint;
    char*              query;
    weather_callback_t callback;
    void*              user_data;
    request_state_t    state;
    uint64_t           start_time;
} weather_request_t;

/**
 * @brief Initialize the weather client
 * @param base_url Base API URL (e.g., "http://localhost:10680/v1")
 * @return 0 on success, -1 on error
 */
int weather_client_init(const char* base_url);

/**
 * @brief Fetch current weather asynchronously
 * @param city City name
 * @param country_code ISO country code (e.g., "SE")
 * @param callback Response callback
 * @param user_data User context
 * @return 0 on success, -1 on error
 */
int weather_client_current_async(const char* city, const char* country_code,
                                 weather_callback_t callback, void* user_data);

/**
 * @brief Fetch weather forecast asynchronously
 * @param city City name
 * @param country_code ISO country code (e.g., "SE")
 * @param days Number of forecast days (1-16)
 * @param callback Response callback
 * @param user_data User context
 * @return 0 on success, -1 on error
 */
int weather_client_forecast_async(const char* city, const char* country_code,
                                  int days, weather_callback_t callback,
                                  void* user_data);

/**
 * @brief Process pending async requests
 * @return Number of requests processed
 */
int weather_client_poll(void);

/**
 * @brief State machine worker - process one step of each request
 * @param current_time Current time in milliseconds
 * @return Number of active requests
 */
int weather_client_smw_work(uint64_t current_time);

/**
 * @brief Get request state string
 * @param state Request state
 * @return Human-readable state name
 */
const char* weather_client_get_state_name(request_state_t state);

/**
 * @brief Cleanup and shutdown client
 */
void weather_client_cleanup(void);

#endif // WEATHER_CLIENT_H
