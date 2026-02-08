#include "config_parser.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_set_defaults(ServerConfig* config) {
    /* Server defaults */
    config->server_port     = 10680;
    config->max_connections = 1024;
    config->daemon_mode     = false;

    /* Cache defaults */
    strncpy(config->cache.cache_dir, "./cache",
            sizeof(config->cache.cache_dir) - 1);
    config->cache.weather_ttl_seconds = 900;    /* 15 minutes */
    config->cache.geo_ttl_seconds     = 604800; /* 7 days */
    config->cache.enabled             = true;

    /* Geocoding defaults */
    config->geocoding.max_results = 10;
    strncpy(config->watchdog.pid_file, "tmp/jws-watchdog.pid",
            sizeof(config->watchdog.pid_file) - 1);
    config->watchdog.max_restarts       = 10;
    config->watchdog.restart_window_sec = 60;
}

int config_parser_load(const char* filepath, ServerConfig* config) {
    if (!filepath || !config) {
        return -1;
    }

    /* Set defaults first */
    config_set_defaults(config);

    /* Try to load JSON config */
    json_error_t error;
    json_t*      root = json_load_file(filepath, 0, &error);

    if (!root) {
        fprintf(stderr, "[CONFIG] Failed to parse %s: %s\n", filepath,
                error.text);
        return -2;
    }

    /* Parse server section */
    json_t* server = json_object_get(root, "server");
    if (server) {
        json_t* port = json_object_get(server, "port");
        if (port && json_is_integer(port)) {
            config->server_port = json_integer_value(port);
        }

        json_t* max_conn = json_object_get(server, "max_connections");
        if (max_conn && json_is_integer(max_conn)) {
            config->max_connections = json_integer_value(max_conn);
        }
    }

    /* Parse cache section */
    json_t* cache = json_object_get(root, "cache");
    if (cache) {
        json_t* cache_dir = json_object_get(cache, "directory");
        if (cache_dir && json_is_string(cache_dir)) {
            strncpy(config->cache.cache_dir, json_string_value(cache_dir),
                    sizeof(config->cache.cache_dir) - 1);
        }
    }

    json_decref(root);
    printf("[CONFIG] Configuration loaded from %s\n", filepath);
    return 0;
}
