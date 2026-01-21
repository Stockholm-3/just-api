/**
 * @file open_meteo_api.c
 * @brief Open-Meteo weather API client with optional filesystem caching.
 *
 * This module provides a synchronous interface for fetching current weather
 * data from the Open-Meteo API. It supports optional on-disk caching of raw
 * JSON responses to reduce network usage and API calls.
 *
 * Features:
 * - HTTP-based weather retrieval using http_client
 * - Optional MD5-based filesystem cache
 * - JSON parsing via Jansson
 * - Wind direction and weather code interpretation
 * - Lazy cache validation using file modification time
 *
 * @note This module is not thread-safe.
 * @note Blocking behavior depends on the http_client + SMW scheduler.
 */

#include <errno.h>
#include <hash_md5.h>
#include <http_client.h>
#include <jansson.h>
#include <open_meteo_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ============= Configuration ============= */

/**
 * @brief Base URL for Open-Meteo API requests.
 */
#define API_BASE_URL "http://api.open-meteo.com/v1/forecast"

/**
 * @brief Default directory used for weather cache storage.
 */
#define DEFAULT_CACHE_DIR "./cache/weather_cache"

/**
 * @brief Default cache TTL in seconds (15 minutes).
 */
#define DEFAULT_CACHE_TTL 900

/* ============= Global State ============= */

/**
 * @brief Global weather API configuration.
 *
 * Initialized with defaults and optionally overridden via
 * open_meteo_api_init().
 */
static WeatherConfig g_config = {.cache_dir = DEFAULT_CACHE_DIR,
                                 .cache_ttl = DEFAULT_CACHE_TTL,
                                 .use_cache = true};

/* ============= Internal Structures ============= */

/**
 * @brief HTTP fetch context for synchronous request emulation.
 *
 * Used to bridge the asynchronous http_client API into a blocking
 * request model by polling SMW until completion.
 */
typedef struct {
  char *response_data;
  size_t response_size;
  int http_status;
  volatile int completed;
  volatile int error;
} HttpFetchContext;

/* ============= Internal Function Prototypes ============= */

static void http_fetch_callback(const char *event, const char *response);
static int fetch_url_sync(const char *url, char **response_data,
                          int *http_status);
static char *generate_cache_filepath(float lat, float lon);
static int is_cache_valid(const char *filepath, int ttl_seconds);
static int load_weather_from_cache(const char *filepath, WeatherData **data);
static int save_raw_json_to_cache(const char *filepath, const char *json_str);
static int fetch_weather_from_api(Location *location, WeatherData **data);
static char *build_api_url(float lat, float lon);
static int parse_weather_json(const char *json_str, WeatherData *data,
                              float lat, float lon);

/* ============= Weather Code Descriptions ============= */

/**
 * @brief Mapping of Open-Meteo weather codes to human-readable descriptions.
 */
static const struct {
  int code;
  const char *description;
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

/* ============= Public Utility Functions ============= */

/**
 * @brief Convert wind direction in degrees to a cardinal direction string.
 *
 * @param degrees Wind direction in degrees.
 *
 * @return Pointer to a static string describing the direction.
 */
const char *open_meteo_api_get_wind_direction(int degrees) {
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

/* ============= Filesystem Helpers ============= */

/**
 * @brief Recursively create a directory path.
 *
 * @param path Directory path to create.
 * @param mode Filesystem permissions.
 *
 * @return 0 on success, -1 on failure.
 */
static int mkdir_recursive(const char *path, mode_t mode) {
  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", path);

  size_t len = strlen(tmp);
  if (len == 0) {
    return -1;
  }

  if (tmp[len - 1] == '/') {
    tmp[len - 1] = '\0';
  }

  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
      }
      *p = '/';
    }
  }

  if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
    return -1;
  }

  return 0;
}

/* ============= HTTP Client Integration ============= */

/**
 * @brief Global pointer to the active HTTP fetch context.
 *
 * Used by the HTTP callback to signal completion.
 */
static HttpFetchContext *g_fetch_context = NULL;

/**
 * @brief HTTP client callback handler.
 *
 * Updates the global fetch context based on response events.
 */
static void http_fetch_callback(const char *event, const char *response) {
  if (!g_fetch_context) {
    return;
  }

  if (strcmp(event, "RESPONSE") == 0) {
    g_fetch_context->response_data = strdup(response);
    g_fetch_context->response_size = strlen(response);
    g_fetch_context->http_status = 200;
    g_fetch_context->completed = 1;
  } else if (strcmp(event, "ERROR") == 0 || strcmp(event, "TIMEOUT") == 0) {
    g_fetch_context->error = 1;
    g_fetch_context->completed = 1;
  }
}

/**
 * @brief Perform a synchronous HTTP GET request.
 *
 * Blocks by polling the SMW scheduler until the request completes or
 * times out.
 *
 * @param url           URL to fetch.
 * @param response_data Output pointer to response string.
 * @param http_status   Output HTTP status code.
 *
 * @return 0 on success, -1 on failure.
 */
static int fetch_url_sync(const char *url, char **response_data,
                          int *http_status) {
  HttpFetchContext context = {0};
  g_fetch_context = &context;

  http_client_get(url, 30000, http_fetch_callback, NULL);

  time_t start_time = time(NULL);
  while (!context.completed) {
    smw_work(0);

    if (time(NULL) - start_time > 30) {
      break;
    }
  }

  g_fetch_context = NULL;

  if (context.error || !context.completed) {
    free(context.response_data);
    return -1;
  }

  *response_data = context.response_data;
  *http_status = context.http_status;
  return 0;
}

/* ============= Public API Implementation ============= */

/**
 * @brief Initialize the Open-Meteo API module.
 *
 * @param config Configuration structure to copy.
 *
 * @return 0 on success, -1 on invalid input.
 */
int open_meteo_api_init(WeatherConfig *config) {
  if (!config) {
    return -1;
  }

  g_config = *config;

  mkdir_recursive(g_config.cache_dir, 0755);
  return 0;
}

/**
 * @brief Fetch current weather for a given location.
 *
 * Uses cached data when enabled and valid, otherwise fetches from the API.
 *
 * @param location Input location.
 * @param data     Output weather data pointer.
 *
 * @return 0 on success, negative value on failure.
 */
int open_meteo_api_get_current(Location *location, WeatherData **data) {
  if (!location || !data) {
    return -1;
  }

  char *cache_file =
      generate_cache_filepath(location->latitude, location->longitude);
  if (!cache_file) {
    return -2;
  }

  if (g_config.use_cache && is_cache_valid(cache_file, g_config.cache_ttl)) {
    if (load_weather_from_cache(cache_file, data) == 0) {
      free(cache_file);
      return 0;
    }
  }

  int result = fetch_weather_from_api(location, data);
  if (result != 0) {
    free(cache_file);
    return -3;
  }

  if (g_config.use_cache && (*data)->_raw_json_cache) {
    save_raw_json_to_cache(cache_file, (*data)->_raw_json_cache);
    free((*data)->_raw_json_cache);
    (*data)->_raw_json_cache = NULL;
  }

  free(cache_file);
  return 0;
}

/**
 * @brief Free a WeatherData structure.
 */
void open_meteo_api_free_current(WeatherData *data) {
  if (data) {
    free(data->_raw_json_cache);
    free(data);
  }
}

/**
 * @brief Get a description string for a weather code.
 */
const char *open_meteo_api_get_description(int weather_code) {
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
