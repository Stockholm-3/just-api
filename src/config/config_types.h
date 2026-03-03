#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H

#include <stdbool.h>

typedef struct {
    /* Server */
    int  server_port;
    int  max_connections;
    bool daemon_mode;

    /* Cache */
    struct {
        char cache_dir[256];
        int  weather_ttl_seconds;
        int  geo_ttl_seconds;
        bool enabled;
    } cache;

    /* Geocoding */
    struct {
        int  max_results;
        char language[8];
    } geocoding;

    /* Watchdog */
    struct {
        char pid_file[256];
        int  max_restarts;
        int  restart_window_sec;
        int  initial_backoff_ms;
        int  max_backoff_ms;
        int  server_ready_wait_ms;
        int  monitor_poll_us;
    } watchdog;

    /* Thread pool */
    struct {
        int num_workers;
        int max_pending;
    } thread_pool;

    /* Energy plan store */
    struct {
        char base_dir[256];
        int  max_cities;
        long city_ttl_seconds;
    } energy_plan;

    /* Fetch scheduler */
    struct {
        char service_host[64];
        char service_port[8];
        char weather_url_path[128];
        char elpris_url_path[128];
        char price_zones[4][8]; /* up to 4 zones, e.g. "SE1" */
        int  price_zones_count;
        long timeout_ms;
        long weather_interval_ms;
        long weather_offset_ms;
        int  elpris_hour_utc;
        int  elpris_minute_utc;
    } scheduler;

    /* Compute */
    struct {
        char cities_csv[256];
        char compute_input_dir[256];
        char elpris_json[256];
        char output_dir[256];
        char lock_file[256];
        char log_dir[256];
    } compute;

    /* Paths (runtime, not persisted) */
    struct {
        char server_binary[256];
        char compute_binary[256];
        char log_dir[256];
    } paths;

} ServerConfig;

void config_set_defaults(ServerConfig* config);

#endif // CONFIG_TYPES_H
