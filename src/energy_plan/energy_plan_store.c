#include "energy_plan_store.h"

#include "csv_registry.h"
#include "file_lock.h"
#include "fs_utils.h"
#include "logger/logger.h"
#include "smw_file_reader.h"
#include "unistd.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MOD "EP_STORE"

typedef struct {
    char cities_csv[512];
    char compute_input_dir[512];
    char compute_output_dir[512];
    char output_lock[512];
    char elpris_json[512];

    int max_cities;
    int initialised;
} EpStore;

static EpStore g_store = {0};

int energy_plan_store_init(const EpStoreConfig* config) {
    if (!config || !config->base_dir || config->max_cities <= 0) {
        LOG_WARN(LOG_MOD, "init: invalid config");
        return -1;
    }

    const char* b = config->base_dir;
    snprintf(g_store.cities_csv, sizeof(g_store.cities_csv), "%s/cities.csv",
             b);
    snprintf(g_store.compute_input_dir, sizeof(g_store.compute_input_dir),
             "%s/compute_input", b);
    snprintf(g_store.compute_output_dir, sizeof(g_store.compute_output_dir),
             "%s/compute_output", b);
    snprintf(g_store.output_lock, sizeof(g_store.output_lock),
             "%s/compute_output/.lock", b);
    snprintf(g_store.elpris_json, sizeof(g_store.elpris_json),
             "%s/compute_input/elpris_merged.json", b);

    g_store.max_cities = config->max_cities;

    if (fs_mkdir(b) != 0) {
        return -1;
    }
    if (fs_mkdir(g_store.compute_input_dir) != 0) {
        return -1;
    }
    if (fs_mkdir(g_store.compute_output_dir) != 0) {
        return -1;
    }

    g_store.initialised = 1;
    LOG_INFO(LOG_MOD, "Store initialised under '%s'", b);
    return 0;
}

void energy_plan_store_shutdown(void) { memset(&g_store, 0, sizeof(g_store)); }

int energy_plan_store_register_city(const char* city, const char* price,
                                    double lat, double lon, void* context,
                                    EpCityOnDone on_done) {
    if (!g_store.initialised) {
        return -1;
    }
    return csv_registry_upsert(g_store.cities_csv, g_store.max_cities, city,
                               price, lat, lon, context, on_done);
}

EpCityList energy_plan_store_load_cities(void) {
    EpCityList result = {NULL, 0};
    if (!g_store.initialised) {
        {
            return result;
        }
    }

    CsvRow* rows  = NULL;
    int     count = 0;
    if (csv_registry_load(g_store.cities_csv, g_store.max_cities, &rows,
                          &count) != 0) {
        return result;
    }

    result.entries = rows;
    result.count   = count;
    return result;
}

int energy_plan_store_get_weather_path(const char* city, double lat, double lon,
                                       char* out, size_t out_size) {
    if (!g_store.initialised || !city || !out) {
        return -1;
    }
    char lower[256];
    fs_to_lower(city, lower, sizeof(lower));
    int n = snprintf(out, out_size, "%s/%s-%.6f-%.6f.json",
                     g_store.compute_input_dir, lower, lat, lon);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

int energy_plan_store_get_elpris_path(char* out, size_t out_size) {
    if (!g_store.initialised || !out) {
        return -1;
    }
    int n = snprintf(out, out_size, "%s", g_store.elpris_json);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

int energy_plan_store_get_output_path(const char* city, const char* zone,
                                      char* out, size_t out_size) {
    if (!g_store.initialised || !city || !zone || !out) {
        return -1;
    }
    int n = snprintf(out, out_size, "%s/%s-%s.json", g_store.compute_output_dir,
                     city, zone);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

int energy_plan_store_save_weather(const char* city, double lat, double lon,
                                   json_t* data) {
    if (!g_store.initialised || !city || !data) {
        return -1;
    }
    char path[512];
    if (energy_plan_store_get_weather_path(city, lat, lon, path,
                                           sizeof(path)) != 0) {
        return -1;
    }
    LOG_INFO(LOG_MOD, "Saving weather for %s -> %s", city, path);
    return fs_write_json_atomic(path, data);
}

int energy_plan_store_save_elpris(json_t* merged_array) {
    if (!g_store.initialised || !merged_array) {
        return -1;
    }
    LOG_INFO(LOG_MOD, "Saving elpris -> %s", g_store.elpris_json);
    return fs_write_json_atomic(g_store.elpris_json, merged_array);
}

int energy_plan_store_acquire_write_lock(void) {
    if (!g_store.initialised) {
        return -1;
    }
    LOG_DEBUG(LOG_MOD, "Waiting for exclusive output lock…");
    int fd = file_lock_acquire_exclusive(g_store.output_lock);
    if (fd >= 0) {
        LOG_DEBUG(LOG_MOD, "Exclusive output lock acquired");
    }
    return fd;
}

void energy_plan_store_release_write_lock(int lock_handle) {
    file_lock_release(lock_handle);
    if (lock_handle >= 0) {
        LOG_DEBUG(LOG_MOD, "Exclusive output lock released");
    }
}

int energy_plan_store_clear_outputs(void) {
    if (!g_store.initialised) {
        return -1;
    }

    DIR* d = opendir(g_store.compute_output_dir);
    if (!d) {
        LOG_WARN(LOG_MOD, "opendir(%s): %s", g_store.compute_output_dir,
                 strerror(errno));
        return -1;
    }

    int            rc = 0;
    struct dirent* ent;
    char           path[640];

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        size_t nl = strlen(ent->d_name);
        if (nl < 5 || strcmp(ent->d_name + nl - 5, ".json") != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", g_store.compute_output_dir,
                 ent->d_name);
        if (unlink(path) != 0) {
            LOG_WARN(LOG_MOD, "unlink(%s): %s", path, strerror(errno));
            rc = -1;
        }
    }
    closedir(d);
    return rc;
}

int energy_plan_store_write_output(const char* city, const char* zone,
                                   json_t* data) {
    if (!g_store.initialised || !city || !zone || !data) {
        return -1;
    }
    char path[512];
    if (energy_plan_store_get_output_path(city, zone, path, sizeof(path)) !=
        0) {
        return -1;
    }
    int rc = fs_write_json_atomic(path, data);
    if (rc == 0) {
        LOG_INFO(LOG_MOD, "Output written: %s", path);
    }
    return rc;
}

typedef struct {
    void*          user_ctx;
    EpOutputOnDone user_cb;
} OutAdapter;

static void sfr_adapter(void* context, SfrStatus status, char* buf,
                        size_t len) {
    OutAdapter*    a = (OutAdapter*)context;
    EpOutputStatus es;
    switch (status) {
    case SFR_OK:
        es = EP_OUTPUT_OK;
        break;
    case SFR_NOT_FOUND:
        es = EP_OUTPUT_NOT_FOUND;
        break;
    case SFR_LOCK_ERROR:
        es = EP_OUTPUT_LOCK_ERROR;
        break;
    default:
        es = EP_OUTPUT_READ_ERROR;
        break;
    }
    a->user_cb(a->user_ctx, es, buf, len);
    free(a);
}

int energy_plan_store_read_output_async(const char* city, const char* price,
                                        void* context, EpOutputOnDone on_done) {
    if (!city || !price || !on_done || !g_store.initialised) {
        return -1;
    }

    char file_path[512];
    if (energy_plan_store_get_output_path(city, price, file_path,
                                          sizeof(file_path)) != 0) {
        return -1;
    }

    OutAdapter* a = malloc(sizeof(*a));
    if (!a) {
        return -1;
    }
    a->user_ctx = context;
    a->user_cb  = on_done;

    if (smw_file_reader_start(g_store.output_lock, file_path, a, sfr_adapter) !=
        0) {
        free(a);
        return -1;
    }
    return 0;
}
