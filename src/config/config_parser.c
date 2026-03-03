#include "config_parser.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_set_defaults(ServerConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));

    cfg->server_port     = 10680;
    cfg->max_connections = 1024;
    cfg->daemon_mode     = false;

    strncpy(cfg->cache.cache_dir, "./cache", sizeof(cfg->cache.cache_dir) - 1);
    cfg->cache.weather_ttl_seconds = 900;
    cfg->cache.geo_ttl_seconds     = 604800;
    cfg->cache.enabled             = true;

    cfg->geocoding.max_results = 10;
    strncpy(cfg->geocoding.language, "en", sizeof(cfg->geocoding.language) - 1);

    strncpy(cfg->watchdog.pid_file, "tmp/jws-watchdog.pid",
            sizeof(cfg->watchdog.pid_file) - 1);
    cfg->watchdog.max_restarts        = 10;
    cfg->watchdog.restart_window_sec  = 60;
    cfg->watchdog.initial_backoff_ms  = 1000;
    cfg->watchdog.max_backoff_ms      = 30000;
    cfg->watchdog.server_ready_wait_ms = 10000;
    cfg->watchdog.monitor_poll_us     = 100000; //100 ms

    cfg->thread_pool.num_workers = 4;
    cfg->thread_pool.max_pending = 256;

    strncpy(cfg->energy_plan.base_dir, "energy_plan",
            sizeof(cfg->energy_plan.base_dir) - 1);
    cfg->energy_plan.max_cities      = 200;
    cfg->energy_plan.city_ttl_seconds = 2L * 24 * 3600;

    strncpy(cfg->scheduler.service_host, "127.0.0.1",
            sizeof(cfg->scheduler.service_host) - 1);
    strncpy(cfg->scheduler.service_port, "10680",
            sizeof(cfg->scheduler.service_port) - 1);
    strncpy(cfg->scheduler.weather_url_path, "/v1/forecast/minutely",
            sizeof(cfg->scheduler.weather_url_path) - 1);
    strncpy(cfg->scheduler.elpris_url_path, "/v1/elpris",
            sizeof(cfg->scheduler.elpris_url_path) - 1);
    cfg->scheduler.price_zones_count = 4;
    strncpy(cfg->scheduler.price_zones[0], "SE1", 7);
    strncpy(cfg->scheduler.price_zones[1], "SE2", 7);
    strncpy(cfg->scheduler.price_zones[2], "SE3", 7);
    strncpy(cfg->scheduler.price_zones[3], "SE4", 7);
    cfg->scheduler.timeout_ms          = 10000;
    cfg->scheduler.weather_interval_ms = 60ULL * 60 * 1000; // 1 hour
    cfg->scheduler.weather_offset_ms   = 60ULL * 2  * 1000; // HH:02:00
    cfg->scheduler.elpris_hour_utc     = 13;
    cfg->scheduler.elpris_minute_utc   = 5;

    strncpy(cfg->compute.cities_csv, "energy_plan/cities.csv",
            sizeof(cfg->compute.cities_csv) - 1);
    strncpy(cfg->compute.compute_input_dir, "cache/compute_input",
            sizeof(cfg->compute.compute_input_dir) - 1);
    strncpy(cfg->compute.elpris_json, "cache/compute_input/elpris_merged.json",
            sizeof(cfg->compute.elpris_json) - 1);
    strncpy(cfg->compute.output_dir, "energy_plan/compute_output",
            sizeof(cfg->compute.output_dir) - 1);
    strncpy(cfg->compute.lock_file, "energy_plan/compute_output/.lock",
            sizeof(cfg->compute.lock_file) - 1);
    strncpy(cfg->compute.log_dir, "logs/energy_parser",
            sizeof(cfg->compute.log_dir) - 1);

    strncpy(cfg->paths.server_binary, "./just-server",
            sizeof(cfg->paths.server_binary) - 1);
    strncpy(cfg->paths.compute_binary, "./compute",
            sizeof(cfg->paths.compute_binary) - 1);
    strncpy(cfg->paths.log_dir, "./logs", sizeof(cfg->paths.log_dir) - 1);
}

#define STR_SET(dst, jval)                                             \
    do {                                                               \
        if ((jval) && json_is_string(jval))                           \
            strncpy((dst), json_string_value(jval), sizeof(dst) - 1); \
    } while (0)

#define INT_SET(dst, jval)                              \
    do {                                                \
        if ((jval) && json_is_integer(jval))            \
            (dst) = (int)json_integer_value(jval);      \
    } while (0)

#define LONG_SET(dst, jval)                             \
    do {                                                \
        if ((jval) && json_is_integer(jval))            \
            (dst) = (long)json_integer_value(jval);     \
    } while (0)

#define BOOL_SET(dst, jval)                             \
    do {                                                \
        if ((jval) && json_is_boolean(jval))            \
            (dst) = json_boolean_value(jval);           \
    } while (0)

int config_parser_load(const char* filepath, ServerConfig* cfg) {
    if (!filepath || !cfg) return -1;

    config_set_defaults(cfg);

    json_error_t err;
    json_t*      root = json_load_file(filepath, 0, &err);
    if (!root) {
        fprintf(stderr, "[CONFIG] Cannot parse %s: %s\n", filepath, err.text);
        return -2;
    }

    json_t* s = json_object_get(root, "server");
    if (s) {
        INT_SET(cfg->server_port,     json_object_get(s, "port"));
        INT_SET(cfg->max_connections, json_object_get(s, "max_connections"));
        BOOL_SET(cfg->daemon_mode,    json_object_get(s, "daemon_mode"));
    }

    json_t* c = json_object_get(root, "cache");
    if (c) {
        STR_SET(cfg->cache.cache_dir,          json_object_get(c, "directory"));
        INT_SET(cfg->cache.weather_ttl_seconds, json_object_get(c, "weather_ttl_seconds"));
        INT_SET(cfg->cache.geo_ttl_seconds,     json_object_get(c, "geo_ttl_seconds"));
        BOOL_SET(cfg->cache.enabled,            json_object_get(c, "enabled"));
    }

    json_t* g = json_object_get(root, "geocoding");
    if (g) {
        INT_SET(cfg->geocoding.max_results, json_object_get(g, "max_results"));
        STR_SET(cfg->geocoding.language,    json_object_get(g, "language"));
    }

    /* --- watchdog --- */
    json_t* w = json_object_get(root, "watchdog");
    if (w) {
        STR_SET(cfg->watchdog.pid_file,            json_object_get(w, "pid_file"));
        INT_SET(cfg->watchdog.max_restarts,         json_object_get(w, "max_restarts"));
        INT_SET(cfg->watchdog.restart_window_sec,   json_object_get(w, "restart_window_sec"));
        INT_SET(cfg->watchdog.initial_backoff_ms,   json_object_get(w, "initial_backoff_ms"));
        INT_SET(cfg->watchdog.max_backoff_ms,       json_object_get(w, "max_backoff_ms"));
        INT_SET(cfg->watchdog.server_ready_wait_ms, json_object_get(w, "server_ready_wait_ms"));
        INT_SET(cfg->watchdog.monitor_poll_us,      json_object_get(w, "monitor_poll_us"));
    }

    json_t* tp = json_object_get(root, "thread_pool");
    if (tp) {
        INT_SET(cfg->thread_pool.num_workers, json_object_get(tp, "num_workers"));
        INT_SET(cfg->thread_pool.max_pending, json_object_get(tp, "max_pending"));
    }

    json_t* ep = json_object_get(root, "energy_plan");
    if (ep) {
        STR_SET(cfg->energy_plan.base_dir,        json_object_get(ep, "base_dir"));
        INT_SET(cfg->energy_plan.max_cities,       json_object_get(ep, "max_cities"));
        LONG_SET(cfg->energy_plan.city_ttl_seconds, json_object_get(ep, "city_ttl_seconds"));
    }

    json_t* sc = json_object_get(root, "scheduler");
    if (sc) {
        STR_SET(cfg->scheduler.service_host,      json_object_get(sc, "service_host"));
        STR_SET(cfg->scheduler.service_port,      json_object_get(sc, "service_port"));
        STR_SET(cfg->scheduler.weather_url_path,  json_object_get(sc, "weather_url_path"));
        STR_SET(cfg->scheduler.elpris_url_path,   json_object_get(sc, "elpris_url_path"));
        LONG_SET(cfg->scheduler.timeout_ms,           json_object_get(sc, "timeout_ms"));
        LONG_SET(cfg->scheduler.weather_interval_ms,  json_object_get(sc, "weather_interval_ms"));
        LONG_SET(cfg->scheduler.weather_offset_ms,    json_object_get(sc, "weather_offset_ms"));
        INT_SET(cfg->scheduler.elpris_hour_utc,   json_object_get(sc, "elpris_hour_utc"));
        INT_SET(cfg->scheduler.elpris_minute_utc, json_object_get(sc, "elpris_minute_utc"));

        json_t* zones = json_object_get(sc, "price_zones");
        if (zones && json_is_array(zones)) {
            int n = (int)json_array_size(zones);
            if (n > 4) n = 4;
            cfg->scheduler.price_zones_count = n;
            for (int i = 0; i < n; i++) {
                json_t* z = json_array_get(zones, i);
                if (z && json_is_string(z))
                    strncpy(cfg->scheduler.price_zones[i],
                            json_string_value(z), 7);
            }
        }
    }

    json_t* co = json_object_get(root, "compute");
    if (co) {
        STR_SET(cfg->compute.cities_csv,        json_object_get(co, "cities_csv"));
        STR_SET(cfg->compute.compute_input_dir, json_object_get(co, "compute_input_dir"));
        STR_SET(cfg->compute.elpris_json,       json_object_get(co, "elpris_json"));
        STR_SET(cfg->compute.output_dir,        json_object_get(co, "output_dir"));
        STR_SET(cfg->compute.lock_file,         json_object_get(co, "lock_file"));
        STR_SET(cfg->compute.log_dir,           json_object_get(co, "log_dir"));
    }

    json_t* pa = json_object_get(root, "paths");
    if (pa) {
        STR_SET(cfg->paths.server_binary,  json_object_get(pa, "server_binary"));
        STR_SET(cfg->paths.compute_binary, json_object_get(pa, "compute_binary"));
        STR_SET(cfg->paths.log_dir,        json_object_get(pa, "log_dir"));
    }

    json_decref(root);
    printf("[CONFIG] Loaded from %s\n", filepath);
    return 0;
}

int config_parser_validate(const ServerConfig* cfg) {
    if (!cfg) return -1;

    if (cfg->server_port <= 0 || cfg->server_port > 65535) {
        fprintf(stderr, "[CONFIG] Invalid server port: %d\n", cfg->server_port);
        return -1;
    }
    if (cfg->cache.weather_ttl_seconds < 60) {
        fprintf(stderr, "[CONFIG] weather_ttl_seconds too low: %d\n",
                cfg->cache.weather_ttl_seconds);
        return -1;
    }
    if (cfg->thread_pool.num_workers <= 0) {
        fprintf(stderr, "[CONFIG] thread_pool.num_workers must be > 0\n");
        return -1;
    }
    if (cfg->thread_pool.max_pending < 0) {
        fprintf(stderr, "[CONFIG] thread_pool.max_pending must be >= 0\n");
        return -1;
    }
    if (cfg->energy_plan.max_cities <= 0) {
        fprintf(stderr, "[CONFIG] energy_plan.max_cities must be > 0\n");
        return -1;
    }
    if (cfg->scheduler.price_zones_count <= 0) {
        fprintf(stderr, "[CONFIG] scheduler.price_zones must not be empty\n");
        return -1;
    }
    return 0;
}

void config_parser_print(const ServerConfig* cfg) {
    printf("\n=== Server Configuration ===\n");

    printf("Server:\n");
    printf("  port:            %d\n", cfg->server_port);
    printf("  max_connections: %d\n", cfg->max_connections);
    printf("  daemon_mode:     %s\n", cfg->daemon_mode ? "yes" : "no");

    printf("Cache:\n");
    printf("  directory:           %s\n", cfg->cache.cache_dir);
    printf("  weather_ttl_seconds: %d\n", cfg->cache.weather_ttl_seconds);
    printf("  geo_ttl_seconds:     %d\n", cfg->cache.geo_ttl_seconds);
    printf("  enabled:             %s\n", cfg->cache.enabled ? "yes" : "no");

    printf("Geocoding:\n");
    printf("  max_results: %d\n", cfg->geocoding.max_results);
    printf("  language:    %s\n", cfg->geocoding.language);

    printf("Watchdog:\n");
    printf("  pid_file:              %s\n", cfg->watchdog.pid_file);
    printf("  max_restarts:          %d\n", cfg->watchdog.max_restarts);
    printf("  restart_window_sec:    %d\n", cfg->watchdog.restart_window_sec);
    printf("  initial_backoff_ms:    %d\n", cfg->watchdog.initial_backoff_ms);
    printf("  max_backoff_ms:        %d\n", cfg->watchdog.max_backoff_ms);
    printf("  server_ready_wait_ms:  %d\n", cfg->watchdog.server_ready_wait_ms);
    printf("  monitor_poll_us:       %d\n", cfg->watchdog.monitor_poll_us);

    printf("Thread pool:\n");
    printf("  num_workers: %d\n", cfg->thread_pool.num_workers);
    printf("  max_pending: %d\n", cfg->thread_pool.max_pending);

    printf("Energy plan:\n");
    printf("  base_dir:         %s\n", cfg->energy_plan.base_dir);
    printf("  max_cities:       %d\n", cfg->energy_plan.max_cities);
    printf("  city_ttl_seconds: %ld\n", cfg->energy_plan.city_ttl_seconds);

    printf("Scheduler:\n");
    printf("  service_host:        %s\n", cfg->scheduler.service_host);
    printf("  service_port:        %s\n", cfg->scheduler.service_port);
    printf("  weather_url_path:    %s\n", cfg->scheduler.weather_url_path);
    printf("  elpris_url_path:     %s\n", cfg->scheduler.elpris_url_path);
    printf("  price_zones:         ");
    for (int i = 0; i < cfg->scheduler.price_zones_count; i++)
        printf("%s%s", cfg->scheduler.price_zones[i],
               i + 1 < cfg->scheduler.price_zones_count ? ", " : "\n");
    printf("  timeout_ms:          %ld\n", cfg->scheduler.timeout_ms);
    printf("  weather_interval_ms: %ld\n", cfg->scheduler.weather_interval_ms);
    printf("  weather_offset_ms:   %ld\n", cfg->scheduler.weather_offset_ms);
    printf("  elpris_hour_utc:     %d\n", cfg->scheduler.elpris_hour_utc);
    printf("  elpris_minute_utc:   %d\n", cfg->scheduler.elpris_minute_utc);

    printf("Compute:\n");
    printf("  cities_csv:        %s\n", cfg->compute.cities_csv);
    printf("  compute_input_dir: %s\n", cfg->compute.compute_input_dir);
    printf("  elpris_json:       %s\n", cfg->compute.elpris_json);
    printf("  output_dir:        %s\n", cfg->compute.output_dir);
    printf("  lock_file:         %s\n", cfg->compute.lock_file);
    printf("  log_dir:           %s\n", cfg->compute.log_dir);

    printf("Paths:\n");
    printf("  server_binary:  %s\n", cfg->paths.server_binary);
    printf("  compute_binary: %s\n", cfg->paths.compute_binary);
    printf("  log_dir:        %s\n", cfg->paths.log_dir);

    printf("============================\n\n");
}
