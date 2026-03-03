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

    /* Thread pool defaults */
    config->thread_pool.num_workers = 4;
    config->thread_pool.max_pending = 256;

    /* Compute defaults */
    strncpy(config->compute.cities_csv, "energy_plan/cities.csv",
            sizeof(config->compute.cities_csv) - 1);
    strncpy(config->compute.compute_input_dir, "cache/compute_input",
            sizeof(config->compute.compute_input_dir) - 1);
    strncpy(config->compute.elpris_json,
            "cache/compute_input/elpris_merged.json",
            sizeof(config->compute.elpris_json) - 1);
    strncpy(config->compute.output_dir, "energy_plan/compute_output",
            sizeof(config->compute.output_dir) - 1);
    strncpy(config->compute.lock_file, "energy_plan/compute_output/.lock",
            sizeof(config->compute.lock_file) - 1);
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

        json_t* daemon = json_object_get(server, "daemon_mode");
        if (daemon && json_is_boolean(daemon)) {
            config->daemon_mode = json_boolean_value(daemon);
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

        json_t* weather_ttl = json_object_get(cache, "weather_ttl_seconds");
        if (weather_ttl && json_is_integer(weather_ttl)) {
            config->cache.weather_ttl_seconds = json_integer_value(weather_ttl);
        }

        json_t* geo_ttl = json_object_get(cache, "geo_ttl_seconds");
        if (geo_ttl && json_is_integer(geo_ttl)) {
            config->cache.geo_ttl_seconds = json_integer_value(geo_ttl);
        }

        json_t* cache_enabled = json_object_get(cache, "enabled");
        if (cache_enabled && json_is_boolean(cache_enabled)) {
            config->cache.enabled = json_boolean_value(cache_enabled);
        }
    }

    /* Parse geocoding section */
    json_t* geocoding = json_object_get(root, "geocoding");
    if (geocoding) {
        json_t* max_results = json_object_get(geocoding, "max_results");
        if (max_results && json_is_integer(max_results)) {
            config->geocoding.max_results = json_integer_value(max_results);
        }

        json_t* language = json_object_get(geocoding, "language");
        if (language && json_is_string(language)) {
            strncpy(config->geocoding.language, json_string_value(language),
                    sizeof(config->geocoding.language) - 1);
        }
    }

    /* Parse watchdog section */
    json_t* watchdog = json_object_get(root, "watchdog");
    if (watchdog) {
        json_t* pid_file = json_object_get(watchdog, "pid_file");
        if (pid_file && json_is_string(pid_file)) {
            strncpy(config->watchdog.pid_file, json_string_value(pid_file),
                    sizeof(config->watchdog.pid_file) - 1);
        }

        json_t* max_restarts = json_object_get(watchdog, "max_restarts");
        if (max_restarts && json_is_integer(max_restarts)) {
            config->watchdog.max_restarts = json_integer_value(max_restarts);
        }

        json_t* restart_window =
            json_object_get(watchdog, "restart_window_sec");
        if (restart_window && json_is_integer(restart_window)) {
            config->watchdog.restart_window_sec =
                json_integer_value(restart_window);
        }
    }

    /* Parse thread_pool section */
    json_t* thread_pool = json_object_get(root, "thread_pool");
    if (thread_pool) {
        json_t* num_workers = json_object_get(thread_pool, "num_workers");
        if (num_workers && json_is_integer(num_workers)) {
            config->thread_pool.num_workers = json_integer_value(num_workers);
        }

        json_t* max_pending = json_object_get(thread_pool, "max_pending");
        if (max_pending && json_is_integer(max_pending)) {
            config->thread_pool.max_pending = json_integer_value(max_pending);
        }
    }

    /* Parse compute section */
    json_t* compute = json_object_get(root, "compute");
    if (compute) {
        json_t* v;
        v = json_object_get(compute, "cities_csv");
        if (v && json_is_string(v))
            strncpy(config->compute.cities_csv, json_string_value(v),
                    sizeof(config->compute.cities_csv) - 1);
        v = json_object_get(compute, "compute_input_dir");
        if (v && json_is_string(v))
            strncpy(config->compute.compute_input_dir, json_string_value(v),
                    sizeof(config->compute.compute_input_dir) - 1);
        v = json_object_get(compute, "elpris_json");
        if (v && json_is_string(v))
            strncpy(config->compute.elpris_json, json_string_value(v),
                    sizeof(config->compute.elpris_json) - 1);
        v = json_object_get(compute, "output_dir");
        if (v && json_is_string(v))
            strncpy(config->compute.output_dir, json_string_value(v),
                    sizeof(config->compute.output_dir) - 1);
        v = json_object_get(compute, "lock_file");
        if (v && json_is_string(v))
            strncpy(config->compute.lock_file, json_string_value(v),
                    sizeof(config->compute.lock_file) - 1);
    }

    json_decref(root);
    printf("[CONFIG] Configuration loaded from %s\n", filepath);
    return 0;
}

int config_parser_validate(const ServerConfig* config) {
    if (!config) {
        return -1;
    }

    /* Validate port range */
    if (config->cache.weather_ttl_seconds < 60) {
        fprintf(stderr, "[CONFIG] Weather TTL too low: %d\n",
                config->cache.weather_ttl_seconds);
        return -1;
    }

    if (config->thread_pool.num_workers <= 0) {
        fprintf(stderr, "[CONFIG] Thread pool num_workers must be > 0\n");
        return -1;
    }

    if (config->thread_pool.max_pending < 0) {
        fprintf(stderr, "[CONFIG] Thread pool max_pending must be >= 0\n");
        return -1;
    }

    return 0;
}

void config_parser_print(const ServerConfig* config) {
    printf("\n=== Server Configuration ===\n");
    printf("Server:\n");
    printf(" Port: %d\n", config->server_port);
    printf("  Max Connections: %d\n", config->max_connections);
    printf("  Daemon Mode: %s\n", config->daemon_mode ? "yes" : "no");

    printf("\nCache:\n");
    printf("  Directory: %s\n", config->cache.cache_dir);
    printf("  Weather TTL: %d seconds\n", config->cache.weather_ttl_seconds);
    printf("  Geo TTL: %d seconds\n", config->cache.geo_ttl_seconds);
    printf("  Enabled: %s\n", config->cache.enabled ? "yes" : "no");

    printf("\nGeocoding:\n");
    printf("  Max Results: %d\n", config->geocoding.max_results);
    printf("  Language: %s\n", config->geocoding.language);

    printf("\nWatchdog:\n");
    printf("  PID File: %s\n", config->watchdog.pid_file);
    printf("  Max Restarts: %d\n", config->watchdog.max_restarts);
    printf("  Restart Window: %d seconds\n",
           config->watchdog.restart_window_sec);
    printf("\nThread Pool:\n");
    printf("  Workers: %d\n", config->thread_pool.num_workers);
    printf("  Max Pending: %d%s\n", config->thread_pool.max_pending,
           config->thread_pool.max_pending == 0 ? " (unlimited)" : "");

    printf("\nCompute:\n");
    printf("  Cities CSV: %s\n", config->compute.cities_csv);
    printf("  Input Dir:  %s\n", config->compute.compute_input_dir);
    printf("  Elpris JSON: %s\n", config->compute.elpris_json);
    printf("  Output Dir: %s\n", config->compute.output_dir);
    printf("  Lock File:  %s\n", config->compute.lock_file);
    printf("===========================\n\n");
}