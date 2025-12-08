/* open_meteo_handler.h -  open_meteo_handler- endpoint handler for HTTP server
 */

#ifndef OPEN_METEO_HANDLER_H
#define OPEN_METEO_HANDLER_H

/**
 * Initialize the weather server module
 * Must be called before handling requests
 *
 * @return 0 on success, non-zero on error
 */
int open_meteo_handler_init(void);

/**
 * Handle GET /v1/current endpoint
 *
 * @param query_string Query parameters (e.g., "lat=37.7749&long=-122.4194")
 * @param response_json Output parameter - allocated JSON response string
 * (caller must free)
 * @param status_code Output parameter - HTTP status code
 *
 * @return 0 on success, -1 on error
 *
 * Example usage in http_server.c:
 *   char* json = NULL;
 *   int status = 0;
 *   open_meteo_handler_current("lat=37.7749&long=-122.4194", &json, &status);
 *   send_http_response(client_fd, status, "application/json", json);
 *   free(json);
 */
int open_meteo_handler_current(const char* query_string, char** response_json,
                               int* status_code);

/**
 * Cleanup the weather server module
 * Should be called on server shutdown
 */
void open_meteo_handler_cleanup(void);

#endif /* OPEN_METEO_HANDLER_H */
