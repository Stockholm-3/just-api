#include "logger.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <jansson.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define CITIES_CSV_PATH "energy_plan/cities.csv"
#define COMPUTE_INPUT_DIR "cache/compute_input"
#define ELPRIS_JSON_PATH "cache/compute_input/elpris_merged.json"
#define COMPUTE_OUTPUT_DIR "energy_plan/compute_output"
#define LOCK_FILE_PATH "energy_plan/compute_output/.lock"
#define LOG_DIR "logs/energy_parser"
#define LOG_MODULE "ENERGY"

#define MAX_CITIES 64
#define SLOTS_PER_DAY 96
#define NUM_PRICE_ZONES 4
#define MAX_FILENAME 512
#define MAX_LINE 1024

typedef struct {
    char   time[32];
    double temperature;
    double humidity;
    double precipitation;
    int    weather_code;
    double windspeed;
    int    wind_direction;
    double pressure;
    int    is_day;
} WeatherPoint;

typedef struct {
    double SEK_per_kWh;
    char   time_start[40];
    char   time_end[40];
} PricePoint;

typedef struct {
    PricePoint zones[NUM_PRICE_ZONES][SLOTS_PER_DAY];
} ElprisData;

typedef struct {
    char         name[128];
    double       latitude;
    double       longitude;
    int          price_zone;
    WeatherPoint weather[SLOTS_PER_DAY];
    int          weather_count;
    PricePoint*  prices;
} CityData;

static int ensure_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/*
 * Delete every regular *.json file inside `dir_path`.
 * Returns 0 on success, -1 if any unlink failed.
 */
static int clear_output_directory(const char* dir_path) {
    DIR* d = opendir(dir_path);
    if (!d) {
        LOG_ERROR(LOG_MODULE, "Cannot open output dir for clearing: %s (%s)",
                  dir_path, strerror(errno));
        return -1;
    }

    int            rc = 0;
    struct dirent* entry;
    char           path[MAX_FILENAME];

    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        size_t nlen = strlen(entry->d_name);
        if (nlen < 5 || strcmp(entry->d_name + nlen - 5, ".json") != 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if (unlink(path) != 0) {
            LOG_ERROR(LOG_MODULE, "Failed to delete '%s': %s", path,
                      strerror(errno));
            rc = -1;
        } else {
            LOG_DEBUG(LOG_MODULE, "Deleted old output: %s", path);
        }
    }
    closedir(d);
    return rc;
}

/*
 * Open (creating if needed) the lock file and acquire an exclusive
 * flock().  Returns the open fd on success, -1 on failure.
 *
 * The REST API side should do:
 *   int lfd = open(LOCK_FILE_PATH, O_RDONLY);
 *   flock(lfd, LOCK_SH);          // blocks while parser holds EX lock
 *   // … read JSON files …
 *   flock(lfd, LOCK_UN);
 *   close(lfd);
 */
static int acquire_exclusive_lock(const char* lock_path) {
    int fd = open(lock_path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        LOG_ERROR(LOG_MODULE, "Cannot open lock file '%s': %s", lock_path,
                  strerror(errno));
        return -1;
    }
    LOG_DEBUG(LOG_MODULE, "Waiting for exclusive lock on %s …", lock_path);
    if (flock(fd, LOCK_EX) != 0) {
        LOG_ERROR(LOG_MODULE, "flock(LOCK_EX) failed on '%s': %s", lock_path,
                  strerror(errno));
        close(fd);
        return -1;
    }
    LOG_DEBUG(LOG_MODULE, "Exclusive lock acquired");
    return fd;
}

static void release_lock(int fd) {
    if (fd < 0) {
        return;
    }
    flock(fd, LOCK_UN);
    close(fd);
    LOG_DEBUG(LOG_MODULE, "Lock released");
}

static int parse_zone(const char* s) {
    if (strstr(s, "SE1")) {
        return 0;
    }
    if (strstr(s, "SE2")) {
        return 1;
    }
    if (strstr(s, "SE3")) {
        return 2;
    }
    if (strstr(s, "SE4")) {
        return 3;
    }
    LOG_WARN(LOG_MODULE, "Unknown price zone '%s', defaulting to SE3", s);
    return 2;
}

static int load_cities(const char* path, CityData cities[], int max_cities) {
    FILE* f = fopen(path, "r");
    if (!f) {
        LOG_ERROR(LOG_MODULE, "Cannot open cities CSV '%s': %s", path,
                  strerror(errno));
        return -1;
    }

    int  count = 0, line_num = 0;
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), f) && count < max_cities) {
        line_num++;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        CityData* c = &cities[count];
        memset(c, 0, sizeof(*c));

        char zone_str[32] = "SE3";
        long timestamp; // unused but required for parsing

        int n = sscanf(line, "%127[^,],%31[^,],%lf,%lf,%ld", c->name, zone_str,
                       &c->latitude, &c->longitude, &timestamp);
        if (n < 4) {
            LOG_WARN(LOG_MODULE, "Malformed CSV line %d: '%s'", line_num, line);
            continue;
        }
        c->price_zone = parse_zone(zone_str);
        LOG_DEBUG(LOG_MODULE, "City: '%s'  lat=%.6f  lon=%.6f  zone=SE%d",
                  c->name, c->latitude, c->longitude, c->price_zone + 1);
        count++;
    }
    fclose(f);
    LOG_INFO(LOG_MODULE, "Loaded %d cities from %s", count, path);
    return count;
}

static int load_weather(const char* path, WeatherPoint wp[], int max_slots) {
    json_error_t err;
    json_t*      root = json_load_file(path, 0, &err);
    if (!root) {
        LOG_ERROR(LOG_MODULE, "JSON parse error in '%s' line %d: %s", path,
                  err.line, err.text);
        return -1;
    }

    json_t* forecast =
        json_object_get(json_object_get(root, "data"), "minutely_forecast");
    if (!json_is_array(forecast)) {
        LOG_ERROR(LOG_MODULE, "No 'data.minutely_forecast' array in %s", path);
        json_decref(root);
        return -1;
    }

    int     count = 0;
    size_t  idx;
    json_t* slot;

    json_array_foreach(forecast, idx, slot) {
        if (count >= max_slots) {
            break;
        }
        WeatherPoint* w = &wp[count];
        memset(w, 0, sizeof(*w));

        const char* t = json_string_value(json_object_get(slot, "time"));
        if (t) {
            strncpy(w->time, t, sizeof(w->time) - 1);
        } else {
            LOG_WARN(LOG_MODULE, "Missing 'time' at weather slot %d in %s",
                     count, path);
        }

        w->temperature =
            json_number_value(json_object_get(slot, "temperature"));
        w->humidity = json_number_value(json_object_get(slot, "humidity"));
        w->precipitation =
            json_number_value(json_object_get(slot, "precipitation"));
        w->weather_code =
            (int)json_integer_value(json_object_get(slot, "weather_code"));
        w->windspeed = json_number_value(json_object_get(slot, "windspeed"));
        w->wind_direction =
            (int)json_integer_value(json_object_get(slot, "wind_direction"));
        w->pressure = json_number_value(json_object_get(slot, "pressure"));
        w->is_day   = (int)json_integer_value(json_object_get(slot, "is_day"));

        count++;
    }

    json_decref(root);
    LOG_DEBUG(LOG_MODULE, "Loaded %d weather slots from %s", count, path);
    return count;
}

static int load_elpris(const char* path, ElprisData* out) {
    json_error_t err;
    json_t*      root = json_load_file(path, 0, &err);
    if (!root) {
        LOG_ERROR(LOG_MODULE, "JSON parse error in '%s' line %d: %s", path,
                  err.line, err.text);
        return -1;
    }
    if (!json_is_array(root)) {
        LOG_ERROR(LOG_MODULE, "Elpris JSON root is not an array: %s", path);
        json_decref(root);
        return -1;
    }

    int    expected = NUM_PRICE_ZONES * SLOTS_PER_DAY;
    size_t total    = json_array_size(root);

    if ((int)total < expected) {
        LOG_WARN(
            LOG_MODULE,
            "Elpris: expected %d entries, found %zu (file may be truncated)",
            expected, total);
    }

    for (int i = 0; i < expected && i < (int)total; i++) {
        json_t*     obj  = json_array_get(root, i);
        int         zone = i / SLOTS_PER_DAY;
        int         slot = i % SLOTS_PER_DAY;
        PricePoint* pp   = &out->zones[zone][slot];
        memset(pp, 0, sizeof(*pp));

        json_t* sek = json_object_get(obj, "SEK_per_kWh");
        if (!sek) {
            LOG_WARN(LOG_MODULE, "Missing SEK_per_kWh at zone=%d slot=%d", zone,
                     slot);
        } else {
            pp->SEK_per_kWh = json_number_value(sek);
        }

        const char* ts = json_string_value(json_object_get(obj, "time_start"));
        const char* te = json_string_value(json_object_get(obj, "time_end"));
        if (ts) {
            strncpy(pp->time_start, ts, sizeof(pp->time_start) - 1);
        }
        if (te) {
            strncpy(pp->time_end, te, sizeof(pp->time_end) - 1);
        }
    }

    json_decref(root);
    LOG_INFO(LOG_MODULE, "Loaded %zu elpris entries from %s", total, path);
    return (int)total;
}

static void build_weather_path(const CityData* c, char* out, int out_len) {
    char name_lower[128];
    strncpy(name_lower, c->name, sizeof(name_lower) - 1);
    name_lower[sizeof(name_lower) - 1] = '\0';
    for (int i = 0; name_lower[i]; i++) {
        name_lower[i] = (char)tolower((unsigned char)name_lower[i]);
    }
    snprintf(out, out_len, "%s/%s-%f-%f.json", COMPUTE_INPUT_DIR, name_lower,
             c->latitude, c->longitude);
}

static int write_output(const CityData* c,
                        const bool      decisions[SLOTS_PER_DAY]) {
    char zone_str[16];
    snprintf(zone_str, sizeof(zone_str), "SE%d", c->price_zone + 1);

    char path[MAX_FILENAME];
    snprintf(path, sizeof(path), "%s/%s-%s.json", COMPUTE_OUTPUT_DIR, c->name,
             zone_str);

    int buy_count = 0;
    for (int i = 0; i < SLOTS_PER_DAY; i++) {
        buy_count += decisions[i] ? 1 : 0;
    }

    /* Build JSON with jansson */
    json_t* root = json_object();
    json_object_set_new(root, "city", json_string(c->name));
    json_object_set_new(root, "latitude", json_real(c->latitude));
    json_object_set_new(root, "longitude", json_real(c->longitude));
    json_object_set_new(root, "price_zone", json_string(zone_str));
    json_object_set_new(root, "slots_total", json_integer(SLOTS_PER_DAY));
    json_object_set_new(root, "slots_buy", json_integer(buy_count));
    json_object_set_new(root, "slots_sell",
                        json_integer(SLOTS_PER_DAY - buy_count));

    json_t* arr = json_array();
    for (int i = 0; i < SLOTS_PER_DAY; i++) {
        const WeatherPoint* w  = &c->weather[i];
        const PricePoint*   pp = &c->prices[i];

        json_t* s = json_object();
        json_object_set_new(s, "slot", json_integer(i));
        json_object_set_new(
            s, "time", json_string((i < c->weather_count) ? w->time : "N/A"));
        json_object_set_new(
            s, "temperature",
            json_real((i < c->weather_count) ? w->temperature : 0.0));
        json_object_set_new(s, "price_sek", json_real(pp->SEK_per_kWh));
        json_object_set_new(s, "action",
                            json_string(decisions[i] ? "buy" : "sell"));
        json_array_append_new(arr, s);
    }
    json_object_set_new(root, "decisions", arr);

    /* Write atomically: write to a temp file, then rename */
    char tmp_path[MAX_FILENAME];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.%s-%s.tmp", COMPUTE_OUTPUT_DIR,
             c->name, zone_str);

    if (json_dump_file(root, tmp_path, JSON_INDENT(2)) != 0) {
        LOG_ERROR(LOG_MODULE, "Failed to write temp output '%s': %s", tmp_path,
                  strerror(errno));
        json_decref(root);
        return -1;
    }
    json_decref(root);

    if (rename(tmp_path, path) != 0) {
        LOG_ERROR(LOG_MODULE, "rename '%s' → '%s' failed: %s", tmp_path, path,
                  strerror(errno));
        unlink(tmp_path);
        return -1;
    }

    LOG_INFO(LOG_MODULE, "Output saved → %s  (buy=%d  sell=%d)", path,
             buy_count, SLOTS_PER_DAY - buy_count);
    return 0;
}

static void run_energy_algorithm(const WeatherPoint weather[SLOTS_PER_DAY],
                                 const PricePoint   prices[SLOTS_PER_DAY],
                                 bool               decisions[SLOTS_PER_DAY],
                                 int                weather_count) {
    (void)weather;
    (void)weather_count;

    // TODO: ADD ACTUALL USEFULL ALGORITHM
    double sum = 0.0;
    for (int i = 0; i < SLOTS_PER_DAY; i++) {
        sum += prices[i].SEK_per_kWh;
    }
    double avg = sum / SLOTS_PER_DAY;
    LOG_DEBUG(LOG_MODULE, "Algo: avg price = %.5f SEK/kWh", avg);

    for (int i = 0; i < SLOTS_PER_DAY; i++) {
        decisions[i] = prices[i].SEK_per_kWh < avg;
    }
}

int main(void) {
    if (logger_init(LOG_DIR, LOG_DEBUG) != 0) {
        fprintf(stderr, "FATAL: could not initialise logger\n");
        return 1;
    }
    LOG_INFO(LOG_MODULE, "=== Energy parser starting ===");

    if (ensure_directory(COMPUTE_OUTPUT_DIR) != 0) {
        LOG_ERROR(LOG_MODULE, "Cannot create output dir '%s': %s",
                  COMPUTE_OUTPUT_DIR, strerror(errno));
        logger_shutdown();
        return 1;
    }

    int lock_fd = acquire_exclusive_lock(LOCK_FILE_PATH);
    if (lock_fd < 0) {
        LOG_ERROR(LOG_MODULE, "Could not acquire output lock – aborting");
        logger_shutdown();
        return 1;
    }

    int exit_code = 0;

    CityData cities[MAX_CITIES];
    int      city_count = load_cities(CITIES_CSV_PATH, cities, MAX_CITIES);
    if (city_count <= 0) {
        LOG_ERROR(LOG_MODULE, "No cities loaded – aborting");
        exit_code = 1;
        goto done;
    }

    ElprisData elpris;
    memset(&elpris, 0, sizeof(elpris));
    if (load_elpris(ELPRIS_JSON_PATH, &elpris) <= 0) {
        LOG_ERROR(LOG_MODULE, "Failed to load elpris data – aborting");
        exit_code = 1;
        goto done;
    }

    LOG_INFO(LOG_MODULE, "Clearing old output files …");
    if (clear_output_directory(COMPUTE_OUTPUT_DIR) != 0) {
        LOG_WARN(LOG_MODULE,
                 "Some old output files could not be deleted – continuing");
    }

    {
        int ok = 0, fail = 0;

        for (int ci = 0; ci < city_count; ci++) {
            CityData* c = &cities[ci];
            LOG_INFO(LOG_MODULE, "--- Processing '%s' (SE%d) ---", c->name,
                     c->price_zone + 1);

            c->prices = elpris.zones[c->price_zone];

            char weather_path[MAX_FILENAME];
            build_weather_path(c, weather_path, sizeof(weather_path));

            int wcount = load_weather(weather_path, c->weather, SLOTS_PER_DAY);
            if (wcount < 0) {
                LOG_ERROR(LOG_MODULE,
                          "Skipping '%s' – weather file not found: %s", c->name,
                          weather_path);
                fail++;
                continue;
            }
            if (wcount < SLOTS_PER_DAY) {
                LOG_WARN(LOG_MODULE, "'%s': only %d/%d weather slots available",
                         c->name, wcount, SLOTS_PER_DAY);
            }
            c->weather_count = wcount;

            bool decisions[SLOTS_PER_DAY];
            memset(decisions, 0, sizeof(decisions));
            run_energy_algorithm(c->weather, c->prices, decisions, wcount);

            if (write_output(c, decisions) != 0) {
                LOG_ERROR(LOG_MODULE, "Failed to write output for '%s'",
                          c->name);
                fail++;
                continue;
            }
            ok++;
        }

        if (fail > 0) {
            LOG_WARN(LOG_MODULE, "=== Finished: %d ok, %d failed ===", ok,
                     fail);
            exit_code = 1;
        } else {
            LOG_INFO(LOG_MODULE,
                     "=== Finished: all %d cities processed ===", ok);
        }
    }

done:
    release_lock(lock_fd);
    logger_shutdown();
    return exit_code;
}
