#include "weather_server_instance.h"

#include "open_meteo_handler.h"
#include "response_builder.h"
#include "weather_location_handler.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-----------------Internal Functions-----------------

int weather_server_instance_on_request(void* context);

//----------------------------------------------------

int weather_server_instance_initiate(WeatherServerInstance* instance,
                                     HTTPServerConnection*  connection) {
    instance->connection = connection;

    http_server_connection_set_callback(instance->connection, instance,
                                        weather_server_instance_on_request);

    return 0;
}

int weather_server_instance_initiate_ptr(HTTPServerConnection*   connection,
                                         WeatherServerInstance** instance_ptr) {
    if (instance_ptr == NULL) {
        return -1;
    }

    WeatherServerInstance* instance =
        (WeatherServerInstance*)malloc(sizeof(WeatherServerInstance));
    if (instance == NULL) {
        return -2;
    }

    int result = weather_server_instance_initiate(instance, connection);
    if (result != 0) {
        free(instance);
        return result;
    }

    *(instance_ptr) = instance;

    return 0;
}

int weather_server_instance_on_request(void* context) {
    WeatherServerInstance* inst = (WeatherServerInstance*)context;
    HTTPServerConnection*  conn = inst->connection;

    printf("[WEATHER] onRequest: %s %s\n", conn->method, conn->request_path);

    // Parse URL to get path and query
    char path[256]  = {0};
    char query[512] = {0};

    char* question_mark = strchr(conn->request_path, '?');
    if (question_mark) {
        size_t path_len = question_mark - conn->request_path;
        strncpy(path, conn->request_path, path_len);
        path[path_len] = '\0';
        strcpy(query, question_mark + 1);
    } else {
        strcpy(path, conn->request_path);
    }

    // ==================================================================
    // ENDPOINT: GET /
    // Homepage with API documentation
    // ==================================================================
    if (strcmp(conn->method, "GET") == 0 && strcmp(path, "/") == 0) {
        printf("[WEATHER] Serving homepage\n");

        const char* html =
            "<!DOCTYPE html>"
            "<html>"
            "<head><title>Just Weather</title></head>"
            "<body>"
            "<h1>Just Weather API</h1>"
            "<p>Available endpoints:</p>"
            "<ul>"
            "  <li><b>GET /echo</b> — echo raw request</li>"
            "  <li><b>POST /echo</b> — echo raw body</li>"
            "  <li><b>GET /v1/current?lat=XX&lon=YY</b> — current weather by "
            "coordinates</li>"
            "  <li><b>GET /v1/weather?city=NAME&country=CODE</b> — weather by "
            "city name</li>"
            "  <li><b>GET /v1/cities?query=SEARCH</b> — city search "
            "(autocomplete)</li>"
            "</ul>"
            "<p>Source code available on <a "
            "href=\"https://github.com/Stockholm-3/just-weather\" "
            "target=\"_blank\">GitHub</a>.</p>"
            "</body>"
            "</html>";

        char header[256];
        int  header_len = snprintf(header, sizeof(header),
                                   "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: text/html; charset=utf-8\r\n"
                                    "Access-Control-Allow-Origin: *\r\n"
                                    "Content-Length: %zu\r\n"
                                    "\r\n",
                                   strlen(html));

        size_t   total = header_len + strlen(html);
        uint8_t* buf   = malloc(total + 1);

        memcpy(buf, header, header_len);
        strcpy((char*)buf + header_len, html);

        conn->write_buffer = buf;
        conn->write_size   = total;
        return 0;
    }

    // ==================================================================
    // ENDPOINT: /echo
    // Echo endpoint for debugging
    // ==================================================================
    if (strcmp(path, "/echo") == 0) {
        printf("[WEATHER] Echo endpoint hit (%s)\n", conn->method);

        size_t body_len = conn->read_buffer_size;

        char header[256];
        int  header_len = snprintf(header, sizeof(header),
                                   "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: text/plain\r\n"
                                    "Access-Control-Allow-Origin: *\r\n"
                                    "Content-Length: %zu\r\n"
                                    "\r\n",
                                   body_len);

        size_t   total = header_len + body_len;
        uint8_t* resp  = malloc(total);

        memcpy(resp, header, header_len);
        memcpy(resp + header_len, conn->read_buffer, body_len);

        conn->write_buffer = resp;
        conn->write_size   = total;
        return 0;
    }

    // ==================================================================
    // ENDPOINT: /v1/weather?city=<name>&country=<code>
    // Weather by city name (uses geocoding + weather API)
    // ==================================================================
    if (strcmp(conn->method, "GET") == 0 && strcmp(path, "/v1/weather") == 0) {
        printf("[WEATHER] Handling /v1/weather request\n");

        char* json_response = NULL;
        int   status_code   = 0;

        // Call the city-based weather handler
        weather_location_handler_by_city(query, &json_response, &status_code);

        if (!json_response) {
            const char* reason = "Failed to fetch weather data for city";

            char* error_json = response_builder_error(
                HTTP_INTERNAL_ERROR,
                response_builder_get_error_type(HTTP_INTERNAL_ERROR), reason);

            if (!error_json) {
                return -1;
            }

            char header[256];
            int  header_len = snprintf(header, sizeof(header),
                                       "HTTP/1.1 500 Internal Server Error\r\n"
                                        "Content-Type: application/json\r\n"
                                        "Access-Control-Allow-Origin: *\r\n"
                                        "Content-Length: %zu\r\n"
                                        "\r\n",
                                       strlen(error_json));

            size_t   total_len = header_len + strlen(error_json);
            uint8_t* response  = malloc(total_len + 1);
            if (response) {
                memcpy(response, header, header_len);
                strcpy((char*)(response + header_len), error_json);

                conn->write_buffer = response;
                conn->write_size   = total_len;
            }

            free(error_json);
            printf("[WEATHER] /v1/weather failed: %s\n", reason);
            return 0;
        }

        // Success: return JSON
        char header[256];
        int  header_len =
            snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Content-Length: %zu\r\n"
                     "\r\n",
                     status_code, status_code == 200 ? "OK" : "Error",
                     strlen(json_response));

        size_t   total_len = header_len + strlen(json_response);
        uint8_t* response  = malloc(total_len + 1);
        if (response) {
            memcpy(response, header, header_len);
            strcpy((char*)(response + header_len), json_response);

            conn->write_buffer = response;
            conn->write_size   = total_len;
        }

        free(json_response);
        return 0;
    }

    // ==================================================================
    // ENDPOINT: /v1/cities?query=<search>
    // City search (autocomplete)
    // ==================================================================
    if (strcmp(conn->method, "GET") == 0 && strcmp(path, "/v1/cities") == 0) {
        printf("[WEATHER] Handling /v1/cities request\n");

        char* json_response = NULL;
        int   status_code   = 0;

        weather_location_handler_search_cities(query, &json_response,
                                               &status_code);

        if (!json_response) {
            const char* reason = "Failed to search cities";

            char* error_json = response_builder_error(
                HTTP_INTERNAL_ERROR,
                response_builder_get_error_type(HTTP_INTERNAL_ERROR), reason);

            if (!error_json) {
                return -1;
            }

            char header[256];
            int  header_len = snprintf(header, sizeof(header),
                                       "HTTP/1.1 500 Internal Server Error\r\n"
                                        "Content-Type: application/json\r\n"
                                        "Access-Control-Allow-Origin: *\r\n"
                                        "Content-Length: %zu\r\n"
                                        "\r\n",
                                       strlen(error_json));

            size_t   total_len = header_len + strlen(error_json);
            uint8_t* response  = malloc(total_len + 1);
            if (response) {
                memcpy(response, header, header_len);
                strcpy((char*)(response + header_len), error_json);

                conn->write_buffer = response;
                conn->write_size   = total_len;
            }

            free(error_json);
            printf("[WEATHER] /v1/cities failed: %s\n", reason);
            return 0;
        }

        // Success: return JSON
        char header[256];
        int  header_len =
            snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Content-Length: %zu\r\n"
                     "\r\n",
                     status_code, status_code == 200 ? "OK" : "Error",
                     strlen(json_response));

        size_t   total_len = header_len + strlen(json_response);
        uint8_t* response  = malloc(total_len + 1);
        if (response) {
            memcpy(response, header, header_len);
            strcpy((char*)(response + header_len), json_response);

            conn->write_buffer = response;
            conn->write_size   = total_len;
        }

        free(json_response);
        return 0;
    }

    // ==================================================================
    // ENDPOINT: /v1/current?lat=<lat>&lon=<lon>
    // Weather by coordinates
    // ==================================================================
    if (strcmp(conn->method, "GET") == 0 && strcmp(path, "/v1/current") == 0) {
        printf("[WEATHER] Handling /v1/current request\n");

        char* json_response = NULL;
        int   status_code   = 0;

        // Call your Open-Meteo handler
        open_meteo_handler_current(query, &json_response, &status_code);

        if (!json_response) {
            const char* reason =
                "Failed to fetch weather data from Open-Meteo API";

            char* error_json = response_builder_error(
                HTTP_INTERNAL_ERROR,
                response_builder_get_error_type(HTTP_INTERNAL_ERROR), reason);

            if (!error_json) {
                return -1;
            }

            char header[256];
            int  header_len = snprintf(header, sizeof(header),
                                       "HTTP/1.1 500 Internal Server Error\r\n"
                                        "Content-Type: application/json\r\n"
                                        "Access-Control-Allow-Origin: *\r\n"
                                        "Content-Length: %zu\r\n"
                                        "\r\n",
                                       strlen(error_json));

            size_t   total_len = header_len + strlen(error_json);
            uint8_t* response  = malloc(total_len + 1);
            if (response) {
                memcpy(response, header, header_len);
                strcpy((char*)(response + header_len), error_json);

                conn->write_buffer = response;
                conn->write_size   = total_len;
            }

            free(error_json);
            printf("[WEATHER] /v1/current failed: %s\n", reason);
            return 0;
        }

        // Success: return JSON from Open-Meteo
        char header[256];
        int  header_len =
            snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Content-Length: %zu\r\n"
                     "\r\n",
                     status_code, status_code == 200 ? "OK" : "Error",
                     strlen(json_response));

        size_t   total_len = header_len + strlen(json_response);
        uint8_t* response  = malloc(total_len + 1);
        if (response) {
            memcpy(response, header, header_len);
            strcpy((char*)(response + header_len), json_response);

            conn->write_buffer = response;
            conn->write_size   = total_len;
        }

        free(json_response);

        return 0;
    }

    // ==================================================================
    // DEFAULT RESPONSE (for unknown endpoints)
    // ==================================================================
    printf("[WEATHER] 404 Not Found: %s %s\n", conn->method, path);

    char detailed_msg[512];
    snprintf(detailed_msg, sizeof(detailed_msg),
             "The requested endpoint '%s %s' was not found. "
             "Available endpoints: GET /, POST /echo, "
             "GET /v1/current?lat=XX&lon=YY, "
             "GET /v1/weather?city=NAME&country=CODE, "
             "GET /v1/cities?query=SEARCH",
             conn->method, path);

    char* json_response = response_builder_error(
        HTTP_NOT_FOUND, response_builder_get_error_type(HTTP_NOT_FOUND),
        detailed_msg);

    if (!json_response) {
        return -1;
    }

    char header[256];
    int  header_len = snprintf(header, sizeof(header),
                               "HTTP/1.1 404 Not Found\r\n"
                                "Content-Type: application/json\r\n"
                                "Access-Control-Allow-Origin: *\r\n"
                                "Content-Length: %zu\r\n"
                                "\r\n",
                               strlen(json_response));

    size_t   total_len = header_len + strlen(json_response);
    uint8_t* response  = malloc(total_len + 1);

    if (response) {
        memcpy(response, header, header_len);
        strcpy((char*)response + header_len, json_response);

        conn->write_buffer = response;
        conn->write_size   = total_len;
    }

    free(json_response);
    return 0;
}

void weather_server_instance_work(WeatherServerInstance* instance,
                                  uint64_t               mon_time) {}

void weather_server_instance_dispose(WeatherServerInstance* instance) {}

void weather_server_instance_dispose_ptr(WeatherServerInstance** instance_ptr) {
    if (instance_ptr == NULL || *(instance_ptr) == NULL) {
        return;
    }

    weather_server_instance_dispose(*(instance_ptr));
    free(*(instance_ptr));
    *(instance_ptr) = NULL;
}
