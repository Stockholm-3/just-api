/* open_meteo_api.h - Open meteo API integration for just-weather server */

#ifndef OPEN_METEO_API_H
#define OPEN_METEO_API_H

#include <stdbool.h>
#include <time.h>

/* Weather data structure */
typedef struct {
    time_t timestamp;
    int    weather_code;

    double temperature;
    char   temperature_unit[16];

    double windspeed;
    char   windspeed_unit[16];

    int  winddirection;
    char winddirection_unit[16];

    double precipitation;
    char   precipitation_unit[16];

    double humidity;
    double pressure;
    int    is_day;

    char  city_name[128];
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

/* Build JSON response for HTTP */
char* open_meteo_api_build_json_response(WeatherData* data, float lat,
                                         float lon);

/* Parse query parameters: lat=X&long=Y or lat=X&lon=Y */
int open_meteo_api_parse_query(const char* query, float* lat, float* lon);

#endif /* open_meteo_api_H */