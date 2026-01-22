#include <cache_utils/file_cache.h>
#include <errno.h>
#include <http_client.h>
#include <jansson.h>
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
    char*        response_data;
    size_t       response_size;
    int          http_status;
    volatile int completed;
    volatile int error;
} HttpFetchContext;

/* ============= Internal Functions ============= */

static void  http_fetch_callback(const char* event, const char* response);
static int   fetch_url_sync(const char* url, char** response_data,
                            int* http_status);
static int   load_weather_from_json(json_t* root, WeatherData** data);
static int   fetch_weather_from_api(Location* location, WeatherData** data);
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

static HttpFetchContext* g_fetch_context = NULL;

static void http_fetch_callback(const char* event, const char* response) {
    if (!g_fetch_context) {
        return;
    }

    if (strcmp(event, "RESPONSE") == 0) {
        g_fetch_context->response_data = strdup(response);
        g_fetch_context->response_size = strlen(response);
        g_fetch_context->http_status   = 200;
        g_fetch_context->completed     = 1;
    } else if (strcmp(event, "ERROR") == 0 || strcmp(event, "TIMEOUT") == 0) {
        g_fetch_context->error     = 1;
        g_fetch_context->completed = 1;
    }
}

static int fetch_url_sync(const char* url, char** response_data,
                          int* http_status) {
    HttpFetchContext context = {0};
    g_fetch_context          = &context;

    http_client_get(url, 30000, http_fetch_callback, NULL);

    // Poll event loop - fast iterations, no sleep!
    time_t start_time      = time(NULL);
    time_t timeout_seconds = 30;

    while (!context.completed) {
        smw_work(0); // Pass 0 if monotonic time unavailable

        // Check timeout (1 second granularity)
        if (time(NULL) - start_time > timeout_seconds) {
            printf("[METEO] Timeout waiting for response\n");
            break;
        }
    }

    g_fetch_context = NULL;

    if (context.error || !context.completed) {
        if (context.response_data) {
            free(context.response_data);
        }
        return -1;
    }

    *response_data = context.response_data;
    *http_status   = context.http_status;

    return 0;
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
        fprintf(stderr, "[METEO] Warning: Failed to initialize cache\n");
    }

    printf("[METEO] API initialized (http_client mode)\n");
    printf("[METEO] Cache dir: %s\n", g_config.cache_dir);
    printf("[METEO] Cache TTL: %d seconds\n", g_config.cache_ttl);
    printf("[METEO] Cache enabled: %s\n", g_config.use_cache ? "yes" : "no");

    return 0;
}

int open_meteo_api_get_current(Location* location, WeatherData** data) {
    if (!location || !data) {
        fprintf(stderr, "[METEO] Invalid parameters\n");
        return -1;
    }

    /* Generate cache key from coordinates */
    char key_input[256];
    snprintf(key_input, sizeof(key_input), "weather_%.6f_%.6f",
             location->latitude, location->longitude);

    char cache_key[FILE_CACHE_KEY_LENGTH];
    if (file_cache_generate_key(g_weather_cache, key_input, cache_key,
                                sizeof(cache_key)) != FILE_CACHE_OK) {
        fprintf(stderr, "[METEO] Failed to generate cache key\n");
        return -2;
    }

    /* Check cache */
    if (g_config.use_cache && file_cache_is_valid(g_weather_cache, cache_key)) {
        printf("[METEO] Cache HIT\n");

        json_t* cached_json = NULL;
        if (file_cache_load_json(g_weather_cache, cache_key,
                                 (void**)&cached_json) == FILE_CACHE_OK) {
            int result = load_weather_from_json(cached_json, data);
            json_decref(cached_json);

            if (result == 0) {
                return 0;
            }
        }

        fprintf(stderr, "[METEO] Cache load failed\n");
    } else {
        printf("[METEO] Cache MISS\n");
    }

    int result = fetch_weather_from_api(location, data);

    if (result != 0) {
        fprintf(stderr, "[METEO] API fetch failed\n");
        return -3;
    }

    /* Save to cache */
    if (g_config.use_cache && (*data)->_raw_json_cache) {
        json_error_t error;
        json_t*      json = json_loadb((*data)->_raw_json_cache,
                                       strlen((*data)->_raw_json_cache), 0, &error);
        if (json) {
            file_cache_save_json(g_weather_cache, cache_key, json);
            json_decref(json);
        }
        free((*data)->_raw_json_cache);
        (*data)->_raw_json_cache = NULL;
    }

    return 0;
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
    printf("[METEO] API cleaned up\n");
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

static int fetch_weather_from_api(Location* location, WeatherData** data) {
    char* url = build_api_url(location->latitude, location->longitude);
    if (!url) {
        return -1;
    }

    printf("[METEO] Fetching: %s\n", url);

    char* response_data = NULL;
    int   http_status   = 0;

    int result = fetch_url_sync(url, &response_data, &http_status);
    free(url);

    if (result != 0 || !response_data) {
        return -2;
    }

    /* debug: raw response suppressed */

    *data = (WeatherData*)calloc(1, sizeof(WeatherData));
    if (!*data) {
        free(response_data);
        return -3;
    }

    result = parse_weather_json(response_data, *data, location->latitude,
                                location->longitude);

    if (result != 0) {
        free(response_data);
        free(*data);
        *data = NULL;
        return -4;
    }

    (*data)->_raw_json_cache = response_data;

    printf("[METEO] Successfully fetched weather data\n");
    return 0;
}
