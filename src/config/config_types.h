#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H

#include <stdbool.h>

typedef struct{
    /* Server Configuration */
    int server_port;
    int max_connections;
    int daemon_mode;

    /* Cache Configuration */
    struct{
        char cache_dir[256];
        int weather_ttl_seconds;
        int geo_ttl_seconds;
        bool enabled;
    } cache;

    /* API Configuration */
    struct{
        int max_results;
        char language[8];
    } geocoding;

    /* Watchdog Configuration */
    struct{
        char pid_file[256];
        int max_restarts;
        int restart_window_sec;
    } watchdog;
} ServerConfig;

/* Default configuration values */
void config_set_defaults(ServerConfig* config);

#endif // CONFIG_TYPES_H