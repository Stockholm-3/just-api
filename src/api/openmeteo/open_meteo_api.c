#include <cache_utils/file_cache.h>
#include <errno.h>
#include <http_client.h>
#include <jansson.h>
#include <logger/logger.h>
#include <open_meteo_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============= Configuration ============= */

#define API_BASE_URL "http://api.open-meteo.com/v1/forecast"
#define DEFAULT_CACHE_DIR "./cache/weather_cache"
#define DEFAULT_CACHE_TTL 900 /* 15 minutes */

/* ============= Global State ============= */

static WeatherConfig g_config = {.cache_dir = DEFAULT_CACHE_DIR,
                                 .cache_ttl = DEFAULT_CACHE_TTL,
                                 .use_cache = true};

/* Cache instance */
static FileCacheInstance* g_weather_cache = NULL;

/* ============= Internal Structures ============= */

typedef struct {
    Location*          location;
    WeatherApiCallback callback;
    void*              user_context;
    char*              cache_key;
    char*              url;
    char*              response_data;
} AsyncWeatherContext;

/* ============= Internal Functions ============= */

static void  http_fetch_async_callback(const char* event, const char* response,
                                       void* context);
static int   load_weather_from_json(json_t* root, WeatherData** data);
static int   fetch_weather_from_api_async(Location*          location,
                                          WeatherApiCallback callback,
                                          void*              user_context,
                                          const char*        cache_key);
static char* build_api_url(float lat, float lon);
static int   parse_weather_json(const char* json_str, WeatherData* data,
                                float lat, float lon);

/* ============= Weather Code Descriptions ============= */

static const struct {
    int         code;
    const char* description;
} WEATHER_DESCRIPTIONS[] = {{0, "Clear sky"},
                            {1, "Mainly clear"},
                            {2, "Partly cloudy"},
                            {3, "Overcast"},
                            {45, "Fog"},
                            {48, "Depositing rime fog"},
                            {51, "Light drizzle"},
                            {53, "Moderate drizzle"},
                            {55, "Dense drizzle"},
                            {61, "Slight rain"},
                            {63, "Moderate rain"},
                            {65, "Heavy rain"},
                            {71, "Slight snow"},
                            {73, "Moderate snow"},
                            {75, "Heavy snow"},
                            {77, "Snow grains"},
                            {80, "Slight rain showers"},
                            {81, "Moderate rain showers"},
                            {82, "Violent rain showers"},
                            {85, "Slight snow showers"},
                            {86, "Heavy snow showers"},
                            {95, "Thunderstorm"},
                            {96, "Thunderstorm with slight hail"},
                            {99, "Thunderstorm with heavy hail"},
                            {-1, "Unknown"}};

/* ============= Wind Direction Cardinal ============= */

const char* open_meteo_api_get_wind_direction(int degrees) {
    degrees = degrees % 360;
    if (degrees < 0) {
        degrees += 360;
    }

    if (degrees >= 348.75 || degrees < 11.25) {
        return "North";
    } else if (degrees < 33.75) {
        return "North-Northeast";
    } else if (degrees < 56.25) {
        return "Northeast";
    } else if (degrees < 78.75) {
        return "East-Northeast";
    } else if (degrees < 101.25) {
        return "East";
    } else if (degrees < 123.75) {
        return "East-Southeast";
    } else if (degrees < 146.25) {
        return "Southeast";
    } else if (degrees < 168.75) {
        return "South-Southeast";
    } else if (degrees < 191.25) {
        return "South";
    } else if (degrees < 213.75) {
        return "South-Southwest";
    } else if (degrees < 236.25) {
        return "Southwest";
    } else if (degrees < 258.75) {
        return "West-Southwest";
    } else if (degrees < 281.25) {
        return "West";
    } else if (degrees < 303.75) {
        return "North-Northwest";
    } else if (degrees < 326.25) {
        return "Northwest";
    } else {
        return "North-Northwest";
    }
}

/* ============= HTTP Client Integration ============= */

static void http_fetch_async_callback(const char* event, const char* response,
                                      void* context) {
    AsyncWeatherContext* ctx = (AsyncWeatherContext*)context;

    if (!ctx) {
        return;
    }

    WeatherData* data   = NULL;
    int          status = -1;

    if (strcmp(event, "RESPONSE") == 0) {
        /* Parse weather data from response */
        data = (WeatherData*)calloc(1, sizeof(WeatherData));
        if (data) {
            int parse_result =
                parse_weather_json(response, data, ctx->location->latitude,
                                   ctx->location->longitude);
            if (parse_result == 0) {
                status = 0;
                LOG_INFO("METEO", "Successfully fetched weather data");
            } else {
                LOG_ERROR("METEO", "Failed to parse weather data");
                free(data);
                data   = NULL;
                status = -4;
            }
        } else {
            LOG_ERROR("METEO", "Failed to allocate weather data");
            status = -3;
        }
    } else if (strcmp(event, "ERROR") == 0) {
        LOG_ERROR("METEO", "HTTP request failed: %s",
                  response ? response : "unknown error");
        status = -2;
    } else if (strcmp(event, "TIMEOUT") == 0) {
        LOG_ERROR("METEO", "HTTP request timed out");
        status = -2;
    }

    /* Invoke user callback */
    if (ctx->callback) {
        ctx->callback(status, data, ctx->user_context);
    }

    /* Cleanup context */
    if (ctx->location) {
        if (ctx->location->name) {
            free((void*)ctx->location->name);
        }
        free(ctx->location);
    }
    if (ctx->cache_key) {
        free(ctx->cache_key);
    }
    if (ctx->url) {
        free(ctx->url);
    }
    free(ctx);
}

/* ============= Public API Implementation ============= */

int open_meteo_api_init(WeatherConfig* config) {
    if (!config) {
        return -1;
    }

    g_config = *config;

    /* Initialize cache */
    FileCacheConfig cache_cfg = {.cache_dir   = g_config.cache_dir,
                                 .ttl_seconds = g_config.cache_ttl,
                                 .enabled     = g_config.use_cache};

    g_weather_cache = file_cache_create(&cache_cfg);
    if (!g_weather_cache) {
        LOG_WARN("METEO", "Failed to initialize cache");
    }

    LOG_INFO("METEO", "API initialized (http_client mode)");
    LOG_INFO("METEO", "Cache dir: %s", g_config.cache_dir);
    LOG_INFO("METEO", "Cache TTL: %d seconds", g_config.cache_ttl);
    LOG_INFO("METEO", "Cache enabled: %s", g_config.use_cache ? "yes" : "no");

    return 0;
}

int open_meteo_api_get_current_async(Location*          location,
                                     WeatherApiCallback callback,
                                     void*              user_context) {
    if (!location || !callback) {
        LOG_WARN("METEO", "Invalid parameters");
        return -1;
    }

    /* Caching is handled at the handler level (open_meteo_handler.c) */

    /* Fetch from API asynchronously */
    return fetch_weather_from_api_async(location, callback, user_context, NULL);
}

int open_meteo_api_get_current(Location* location, WeatherData** data) {
    if (!location || !data) {
        LOG_WARN("METEO", "Invalid parameters");
        return -1;
    }

    LOG_WARN("METEO", "open_meteo_api_get_current() is deprecated. "
                      "Use open_meteo_api_get_current_async() instead.");

    /* This synchronous version is kept for backward compatibility
     * but should be migrated to the async version */

    /* Generate cache key from coordinates */
    char key_input[256];
    snprintf(key_input, sizeof(key_input), "weather_%.6f_%.6f",
             location->latitude, location->longitude);

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (file_cache_generate_key(g_weather_cache, key_input, cache_key,
                                sizeof(cache_key)) != FILE_CACHE_OK) {
        LOG_ERROR("METEO", "Failed to generate cache key");
        return -2;
    }

    /* Check cache */
    if (g_config.use_cache && file_cache_is_valid(g_weather_cache, cache_key)) {
        LOG_DEBUG("METEO", "Cache HIT");

        json_t* cached_json = NULL;
        if (file_cache_load_json(g_weather_cache, cache_key,
                                 (void**)&cached_json) == FILE_CACHE_OK) {
            int result = load_weather_from_json(cached_json, data);
            json_decref(cached_json);

            if (result == 0) {
                return 0;
            }
        }

        LOG_ERROR("METEO", "Cache load failed");
    } else {
        LOG_DEBUG("METEO", "Cache MISS");
    }

    LOG_ERROR("METEO", "Synchronous API fetch not supported in async mode.");
    LOG_ERROR("METEO", "Please migrate to open_meteo_api_get_current_async().");
    return -99;
}

void open_meteo_api_free_current(WeatherData* data) {
    if (data) {
        if (data->_raw_json_cache) {
            free(data->_raw_json_cache);
        }
        free(data);
    }
}

void open_meteo_api_cleanup(void) {
    if (g_weather_cache) {
        file_cache_destroy(g_weather_cache);
        g_weather_cache = NULL;
    }
    LOG_INFO("METEO", "API cleaned up");
}

const char* open_meteo_api_get_description(int weather_code) {
    for (size_t i = 0;
         i < sizeof(WEATHER_DESCRIPTIONS) / sizeof(WEATHER_DESCRIPTIONS[0]) - 1;
         i++) {
        if (WEATHER_DESCRIPTIONS[i].code == weather_code) {
            return WEATHER_DESCRIPTIONS[i].description;
        }
    }
    return WEATHER_DESCRIPTIONS[sizeof(WEATHER_DESCRIPTIONS) /
                                    sizeof(WEATHER_DESCRIPTIONS[0]) -
                                1]
        .description;
}

int open_meteo_api_parse_query(const char* query, float* lat, float* lon) {
    if (!query || !lat || !lon) {
        return -1;
    }

    char query_copy[512];
    strncpy(query_copy, query, sizeof(query_copy) - 1);
    query_copy[sizeof(query_copy) - 1] = '\0';

    char* token     = strtok(query_copy, "&");
    int   found_lat = 0, found_lon = 0;

    while (token != NULL) {
        if (strncmp(token, "lat=", 4) == 0) {
            *lat      = atof(token + 4);
            found_lat = 1;
        } else if (strncmp(token, "lon=", 4) == 0 ||
                   strncmp(token, "long=", 5) == 0) {
            char* value = strchr(token, '=');
            if (value) {
                *lon      = atof(value + 1);
                found_lon = 1;
            }
        }
        token = strtok(NULL, "&");
    }

    return (found_lat && found_lon) ? 0 : -1;
}

/* ============= Internal Functions ============= */

/**
 * Load weather data from parsed JSON object
 */
static int load_weather_from_json(json_t* root, WeatherData** data) {
    if (!root) {
        return -1;
    }

    *data = (WeatherData*)calloc(1, sizeof(WeatherData));
    if (!*data) {
        return -2;
    }

    json_t* current       = json_object_get(root, "current");
    json_t* current_units = json_object_get(root, "current_units");

    if (!current || !current_units) {
        free(*data);
        return -3;
    }

    /* Parse all weather data fields */
    json_t* temp = json_object_get(current, "temperature_2m");
    if (temp) {
        (*data)->temperature = json_real_value(temp);
    }

    json_t* windspeed = json_object_get(current, "wind_speed_10m");
    if (windspeed) {
        (*data)->windspeed = json_real_value(windspeed);
    }

    json_t* winddirection = json_object_get(current, "wind_direction_10m");
    if (winddirection) {
        (*data)->winddirection = json_integer_value(winddirection);
    }

    json_t* precipitation = json_object_get(current, "precipitation");
    if (precipitation) {
        (*data)->precipitation = json_real_value(precipitation);
    }

    json_t* humidity = json_object_get(current, "relative_humidity_2m");
    if (humidity) {
        (*data)->humidity = json_real_value(humidity);
    }

    json_t* pressure = json_object_get(current, "surface_pressure");
    if (pressure) {
        (*data)->pressure = json_real_value(pressure);
    }

    json_t* weather_code = json_object_get(current, "weather_code");
    if (weather_code) {
        (*data)->weather_code = json_integer_value(weather_code);
    }

    json_t* is_day = json_object_get(current, "is_day");
    if (is_day) {
        (*data)->is_day = json_integer_value(is_day);
    }

    /* Parse units */
    json_t* temp_unit = json_object_get(current_units, "temperature_2m");
    if (temp_unit && json_is_string(temp_unit)) {
        strncpy((*data)->temperature_unit, json_string_value(temp_unit),
                sizeof((*data)->temperature_unit) - 1);
    } else {
        strcpy((*data)->temperature_unit, "°C");
    }

    json_t* wind_unit = json_object_get(current_units, "wind_speed_10m");
    if (wind_unit && json_is_string(wind_unit)) {
        strncpy((*data)->windspeed_unit, json_string_value(wind_unit),
                sizeof((*data)->windspeed_unit) - 1);
    } else {
        strcpy((*data)->windspeed_unit, "km/h");
    }

    json_t* latitude  = json_object_get(root, "latitude");
    json_t* longitude = json_object_get(root, "longitude");

    if (latitude) {
        (*data)->latitude = json_real_value(latitude);
    }
    if (longitude) {
        (*data)->longitude = json_real_value(longitude);
    }

    return 0;
}

static char* build_api_url(float lat, float lon) {
    char* url = malloc(1024);
    if (!url) {
        return NULL;
    }

    snprintf(url, 1024,
             "%s?latitude=%.6f&longitude=%.6f"
             "&current=temperature_2m,relative_humidity_2m,"
             "apparent_temperature,is_day,precipitation,weather_code,"
             "surface_pressure,wind_speed_10m,wind_direction_10m"
             "&timezone=GMT",
             API_BASE_URL, lat, lon);

    return url;
}

static int parse_weather_json(const char* json_str, WeatherData* data,
                              float lat, float lon) {
    json_error_t error;
    json_t*      root = json_loadb(json_str, strlen(json_str), 0, &error);

    if (!root) {
        return -1;
    }

    json_t* current       = json_object_get(root, "current");
    json_t* current_units = json_object_get(root, "current_units");

    if (!current) {
        json_decref(root);
        return -2;
    }

    // Parse all fields
    json_t* temp = json_object_get(current, "temperature_2m");
    if (temp) {
        data->temperature = json_real_value(temp);
    }

    json_t* windspeed = json_object_get(current, "wind_speed_10m");
    if (windspeed) {
        data->windspeed = json_real_value(windspeed);
    }

    json_t* winddirection = json_object_get(current, "wind_direction_10m");
    if (winddirection) {
        data->winddirection = json_integer_value(winddirection);
    }

    json_t* precipitation = json_object_get(current, "precipitation");
    if (precipitation) {
        data->precipitation = json_real_value(precipitation);
    }

    json_t* humidity = json_object_get(current, "relative_humidity_2m");
    if (humidity) {
        data->humidity = json_real_value(humidity);
    }

    json_t* pressure = json_object_get(current, "surface_pressure");
    if (pressure) {
        data->pressure = json_real_value(pressure);
    }

    json_t* weather_code = json_object_get(current, "weather_code");
    if (weather_code) {
        data->weather_code = json_integer_value(weather_code);
    }

    json_t* is_day = json_object_get(current, "is_day");
    if (is_day) {
        data->is_day = json_integer_value(is_day);
    }

    /* Parse units */
    if (current_units) {
        json_t* temp_unit = json_object_get(current_units, "temperature_2m");
        if (temp_unit && json_is_string(temp_unit)) {
            strncpy(data->temperature_unit, json_string_value(temp_unit),
                    sizeof(data->temperature_unit) - 1);
        } else {
            strcpy(data->temperature_unit, "°C");
        }

        json_t* wind_unit = json_object_get(current_units, "wind_speed_10m");
        if (wind_unit && json_is_string(wind_unit)) {
            strncpy(data->windspeed_unit, json_string_value(wind_unit),
                    sizeof(data->windspeed_unit) - 1);
        } else {
            strcpy(data->windspeed_unit, "km/h");
        }
    } else {
        strcpy(data->temperature_unit, "°C");
        strcpy(data->windspeed_unit, "km/h");
    }

    data->latitude  = lat;
    data->longitude = lon;

    json_decref(root);
    return 0;
}

static int fetch_weather_from_api_async(Location*          location,
                                        WeatherApiCallback callback,
                                        void*              user_context,
                                        const char*        cache_key) {
    char* url = build_api_url(location->latitude, location->longitude);
    if (!url) {
        return -1;
    }

    LOG_DEBUG("METEO", "Fetching: %s", url);

    /* Allocate async context */
    AsyncWeatherContext* ctx =
        (AsyncWeatherContext*)calloc(1, sizeof(AsyncWeatherContext));
    if (!ctx) {
        free(url);
        return -2;
    }

    /* Store location data (we need to copy it) */
    ctx->location = (Location*)malloc(sizeof(Location));
    if (!ctx->location) {
        free(ctx);
        free(url);
        return -3;
    }

    ctx->location->latitude  = location->latitude;
    ctx->location->longitude = location->longitude;
    ctx->location->name      = location->name ? strdup(location->name) : NULL;

    ctx->callback     = callback;
    ctx->user_context = user_context;
    ctx->cache_key    = cache_key ? strdup(cache_key) : NULL;
    ctx->url          = url;

    /* Start async HTTP request (30 second timeout) */
    int result =
        http_client_get(url, NULL, 30000, http_fetch_async_callback, ctx);

    if (result != 0) {
        LOG_ERROR("METEO", "Failed to start HTTP request");
        if (ctx->location->name) {
            free((void*)ctx->location->name);
        }
        free(ctx->location);
        if (ctx->cache_key) {
            free(ctx->cache_key);
        }
        free(ctx->url);
        free(ctx);
        return -4;
    }

    return 0;
}
