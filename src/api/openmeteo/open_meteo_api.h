/* open_meteo_api.h - Open meteo API integration for just-weather server */

#ifndef OPEN_METEO_API_H
#define OPEN_METEO_API_H

#include <stdbool.h>

/* Weather data structure */
typedef struct {
    int weather_code;

    double temperature;
    char   temperature_unit[16];

    double windspeed;
    char   windspeed_unit[16];

    int winddirection;

    double precipitation;

    double humidity;
    double pressure;
    int    is_day;

    float latitude;
    float longitude;

    /* Internal: raw JSON from API (for caching) - DO NOT USE DIRECTLY */
    char* _raw_json_cache;
} WeatherData;

/* Location structure */
typedef struct {
    float       latitude;
    float       longitude;
    const char* name;
} Location;

/* Configuration */
typedef struct {
    const char* cache_dir;
    int         cache_ttl;
    bool        use_cache;
} WeatherConfig;

/* Initialize weather API */
int open_meteo_api_init(WeatherConfig* config);

/* Get current weather for location */
int open_meteo_api_get_current(Location* location, WeatherData** data);

/* Free weather data */
void open_meteo_api_free_current(WeatherData* data);

/* Cleanup */
void open_meteo_api_cleanup(void);

/* Get weather description from code */
const char* open_meteo_api_get_description(int weather_code);

/* Get wind direction name from degrees (North, South-Southeast, etc.) */
const char* open_meteo_api_get_wind_direction(int degrees);

/* Parse query parameters: lat=X&long=Y or lat=X&lon=Y */
int open_meteo_api_parse_query(const char* query, float* lat, float* lon);

#endif /* open_meteo_api_H */