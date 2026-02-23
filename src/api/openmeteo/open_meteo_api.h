/* open_meteo_api.h - Open-Meteo API-integration för just-weather-servern */

#ifndef OPEN_METEO_API_H
#define OPEN_METEO_API_H

#include <stdbool.h>

/* Datastruktur för väderdata */
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

    /* Internt: rå JSON från API (för cachelagring) - ANVÄND INTE DIREKT */
    char* _raw_json_cache;
} WeatherData;

/* Platsstruktur */
typedef struct {
    float       latitude;
    float       longitude;
    const char* name;
} Location;

/* Konfiguration */
typedef struct {
    const char* cache_dir;
    int         cache_ttl;
    bool        use_cache;
} WeatherConfig;

/* Callback-typ för asynkron hämtning av väderdata */
typedef void (*WeatherApiCallback)(int status, WeatherData* data,
                                   void* context);

/* Initiera väder-API */
int open_meteo_api_init(WeatherConfig* config);

/* Hämta aktuellt väder för plats (asynkront med callback) */
int open_meteo_api_get_current_async(Location*          location,
                                     WeatherApiCallback callback,
                                     void*              context);

/* Hämta aktuellt väder för plats (äldre synkron version - föråldrad) */
int open_meteo_api_get_current(Location* location, WeatherData** data);

/* Frigör väderdata */
void open_meteo_api_free_current(WeatherData* data);

/* Rensning */
void open_meteo_api_cleanup(void);

/* Hämta väderbeskrivning från kod */
const char* open_meteo_api_get_description(int weather_code);

/* Hämta vindriktninsnamn från grader (Norr, Syd-Sydost, osv.) */
const char* open_meteo_api_get_wind_direction(int degrees);

/* Parsa frågeparametrar: lat=X&long=Y eller lat=X&lon=Y */
int open_meteo_api_parse_query(const char* query, float* lat, float* lon);

#endif /* OPEN_METEO_API_H */