#include "energy_plan_store.h"

#include "logger/logger.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <libgen.h>
#include <smw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG_MOD "EP_STORE"

typedef struct {
    char cities_csv[512];         // base_dir/cities.csv
    char compute_input_dir[512];  // base_dir/compute_input
    char compute_output_dir[512]; // base_dir/compute_output
    char output_lock[512];        // base_dir/compute_output/.lock
    char elpris_json[512];        // base_dir/compute_input/elpris_merged.json

    int           max_cities;
    unsigned long city_ttl_seconds;

    int initialised;
} EpStore;

static EpStore g_store = {0};

static int ep_mkdir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        LOG_WARN(LOG_MOD, "mkdir(%s): %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * Write @p data as JSON to @p path atomically:
 *   1. unlink existing file (so consumers never see half-written data)
 *   2. write to path.tmp
 *   3. rename into place
 */
static int ep_write_json_atomic(const char* path, json_t* data) {
    if (unlink(path) != 0 && errno != ENOENT) {
        LOG_WARN(LOG_MOD, "unlink(%s): %s", path, strerror(errno));
    }

    char tmp[640];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        LOG_WARN(LOG_MOD, "path too long: %s", path);
        return -1;
    }

    if (json_dump_file(data, tmp, JSON_INDENT(2)) != 0) {
        LOG_WARN(LOG_MOD, "json_dump_file(%s) failed", tmp);
        unlink(tmp);
        return -1;
    }

    if (rename(tmp, path) != 0) {
        LOG_WARN(LOG_MOD, "rename(%s -> %s): %s", tmp, path, strerror(errno));
        unlink(tmp);
        return -1;
    }

    return 0;
}

static void ep_to_lower(const char* src, char* dst, size_t dst_size) {
    size_t i = 0;
    for (; i + 1 < dst_size && src[i]; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

int energy_plan_store_init(const EpStoreConfig* config) {
    if (!config || !config->base_dir || config->max_cities <= 0) {
        LOG_WARN(LOG_MOD, "energy_plan_store_init: invalid config");
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

    g_store.max_cities       = config->max_cities;
    g_store.city_ttl_seconds = config->city_ttl_seconds;

    /* Create directory tree. */
    if (ep_mkdir(b) != 0) {
        return -1;
    }
    if (ep_mkdir(g_store.compute_input_dir) != 0) {
        return -1;
    }
    if (ep_mkdir(g_store.compute_output_dir) != 0) {
        return -1;
    }

    g_store.initialised = 1;
    LOG_INFO(LOG_MOD, "Store initialised under '%s'", b);
    return 0;
}

void energy_plan_store_shutdown(void) { memset(&g_store, 0, sizeof(g_store)); }

#define REG_CHUNK 4096

typedef enum {
    REG_INIT,
    REG_OPEN,
    REG_LOCK,
    REG_READ,
    REG_PROCESS,
    REG_SEEK,
    REG_WRITE,
    REG_DONE,
    REG_ERROR,
    REG_DISPOSE,
} RegState;

typedef struct {
    void* task;

    int fd;

    char*  rbuf;
    size_t rbuf_len;
    size_t rbuf_cap;

    char*  wbuf;
    size_t wbuf_len;
    size_t wbuf_off;

    // Dynamically allocated based on g_store.max_cities.
    EpCityEntry* cities;
    int          city_count;

    char   city[256];
    char   price[16];
    double lat;
    double lon;

    EpCityRegisterStatus result;
    RegState             state;

    void*        cb_ctx;
    EpCityOnDone on_done;
} RegTask;

static void reg_process_csv(RegTask* t) {
    t->city_count = 0;
    int    found  = 0;
    time_t now    = time(NULL);

    char* cur = t->rbuf;
    char* end = t->rbuf + t->rbuf_len;

    while (cur < end && t->city_count < g_store.max_cities) {
        char*  nl   = memchr(cur, '\n', (size_t)(end - cur));
        size_t llen = nl ? (size_t)(nl - cur) : (size_t)(end - cur);
        if (llen == 0) {
            cur = nl ? nl + 1 : end;
            continue;
        }

        char line[512];
        if (llen >= sizeof(line)) {
            llen = sizeof(line) - 1;
        }
        memcpy(line, cur, llen);
        line[llen] = '\0';
        cur        = nl ? nl + 1 : end;

        int   idx = t->city_count;
        char* sp  = NULL;
        char* tok;

        tok = strtok_r(line, ",", &sp);
        if (!tok) {
            continue;
        }
        strncpy(t->cities[idx].city, tok, sizeof(t->cities[idx].city) - 1);

        tok = strtok_r(NULL, ",", &sp);
        strncpy(t->cities[idx].price, tok ? tok : "",
                sizeof(t->cities[idx].price) - 1);

        tok                = strtok_r(NULL, ",", &sp);
        t->cities[idx].lat = tok ? atof(tok) : 0.0;

        tok                = strtok_r(NULL, ",", &sp);
        t->cities[idx].lon = tok ? atof(tok) : 0.0;

        tok                          = strtok_r(NULL, ",", &sp);
        t->cities[idx].last_accessed = tok ? (time_t)atoll(tok) : now;

        if (strcmp(t->cities[idx].city, t->city) == 0 &&
            strcmp(t->cities[idx].price, t->price) == 0) {
            found                        = 1;
            t->cities[idx].last_accessed = now;
        }

        if (now - t->cities[idx].last_accessed >
            (time_t)g_store.city_ttl_seconds) {
            t->cities[idx].city[0] = '\0'; /* mark expired */
        }

        t->city_count++;
    }

    if (found) {
        t->result = EP_CITY_EXISTS;
        return;
    }

    int valid = 0;
    for (int i = 0; i < t->city_count; i++) {
        if (t->cities[i].city[0] != '\0') {
            valid++;
        }
    }
    if (valid >= g_store.max_cities) {
        t->result = EP_CITY_LIMIT_REACHED;
        return;
    }

    int idx = t->city_count;
    strncpy(t->cities[idx].city, t->city, sizeof(t->cities[idx].city) - 1);
    strncpy(t->cities[idx].price, t->price, sizeof(t->cities[idx].price) - 1);
    t->cities[idx].lat           = t->lat;
    t->cities[idx].lon           = t->lon;
    t->cities[idx].last_accessed = now;
    t->city_count++;
    t->result = EP_CITY_ADDED;
}

static int reg_build_write_buf(RegTask* t) {
    size_t cap = (size_t)(t->city_count + 1) * 512;
    char*  buf = malloc(cap);
    if (!buf) {
        return -1;
    }

    size_t off = 0;
    for (int i = 0; i < t->city_count; i++) {
        if (t->cities[i].city[0] == '\0') {
            continue;
        }
        int w =
            snprintf(buf + off, cap - off, "%s,%s,%.6f,%.6f,%ld\n",
                     t->cities[i].city, t->cities[i].price, t->cities[i].lat,
                     t->cities[i].lon, (long)t->cities[i].last_accessed);
        if (w < 0 || (size_t)w >= cap - off) {
            free(buf);
            return -1;
        }
        off += (size_t)w;
    }
    t->wbuf     = buf;
    t->wbuf_len = off;
    t->wbuf_off = 0;
    return 0;
}

static void reg_open(RegTask* t) {
    int fd = open(g_store.cities_csv, O_RDWR | O_NONBLOCK);
    if (fd < 0 && errno == ENOENT) {
        fd = open(g_store.cities_csv, O_RDWR | O_CREAT | O_NONBLOCK, 0644);
    }
    if (fd < 0) {
        LOG_WARN(LOG_MOD, "open(%s): %s", g_store.cities_csv, strerror(errno));
        t->state = REG_ERROR;
        return;
    }
    t->fd    = fd;
    t->state = REG_LOCK;
}

static void reg_lock(RegTask* t) {
    if (flock(t->fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            return;
        }
        LOG_WARN(LOG_MOD, "flock(EX): %s", strerror(errno));
        t->state = REG_ERROR;
        return;
    }
    t->state = REG_READ;
}

static void reg_read(RegTask* t) {
    char    chunk[REG_CHUNK];
    ssize_t n = read(t->fd, chunk, sizeof(chunk));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        LOG_WARN(LOG_MOD, "read: %s", strerror(errno));
        t->state = REG_ERROR;
        return;
    }
    if (n > 0) {
        size_t need = t->rbuf_len + (size_t)n;
        if (need > t->rbuf_cap) {
            size_t nc = need + REG_CHUNK;
            char*  nb = realloc(t->rbuf, nc);
            if (!nb) {
                t->state = REG_ERROR;
                return;
            }
            t->rbuf     = nb;
            t->rbuf_cap = nc;
        }
        memcpy(t->rbuf + t->rbuf_len, chunk, (size_t)n);
        t->rbuf_len += (size_t)n;
        return;
    }
    /* EOF */
    t->state = REG_PROCESS;
}

static void reg_process(RegTask* t) {
    reg_process_csv(t);
    if (t->result == EP_CITY_LIMIT_REACHED) {
        t->state = REG_DONE;
        return;
    }
    if (reg_build_write_buf(t) != 0) {
        t->state = REG_ERROR;
        return;
    }
    t->state = REG_SEEK;
}

static void reg_seek(RegTask* t) {
    if (lseek(t->fd, 0, SEEK_SET) != 0 || ftruncate(t->fd, 0) != 0) {
        LOG_WARN(LOG_MOD, "lseek/ftruncate: %s", strerror(errno));
        t->state = REG_ERROR;
        return;
    }
    t->state = REG_WRITE;
}

static void reg_write(RegTask* t) {
    while (t->wbuf_off < t->wbuf_len) {
        ssize_t n =
            write(t->fd, t->wbuf + t->wbuf_off, t->wbuf_len - t->wbuf_off);
        if (n > 0) {
            t->wbuf_off += (size_t)n;
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        LOG_WARN(LOG_MOD, "write: %s", strerror(errno));
        t->state = REG_ERROR;
        return;
    }
    t->state = REG_DONE;
}

static void reg_dispose(RegTask* t) {
    if (t->task) {
        smw_destroy_task(t->task);
        t->task = NULL;
    }
    if (t->fd >= 0) {
        flock(t->fd, LOCK_UN);
        close(t->fd);
        t->fd = -1;
    }
    free(t->rbuf);
    free(t->wbuf);
    free(t->cities);
    memset(t, 0, sizeof(*t));
    free(t);
}

void ep_registry_task_work(void* context, uint64_t mon_time) {
    (void)mon_time;
    RegTask* t = (RegTask*)context;
    switch (t->state) {
    case REG_INIT:
        t->state = REG_OPEN;
        break;
    case REG_OPEN:
        reg_open(t);
        break;
    case REG_LOCK:
        reg_lock(t);
        break;
    case REG_READ:
        reg_read(t);
        break;
    case REG_PROCESS:
        reg_process(t);
        break;
    case REG_SEEK:
        reg_seek(t);
        break;
    case REG_WRITE:
        reg_write(t);
        break;
    case REG_DONE:
        t->on_done(t->cb_ctx, t->result);
        t->state = REG_DISPOSE;
        break;
    case REG_ERROR:
        LOG_WARN(LOG_MOD, "Registry task failed for city '%s'", t->city);
        t->on_done(t->cb_ctx, EP_CITY_LIMIT_REACHED);
        t->state = REG_DISPOSE;
        break;
    case REG_DISPOSE:
        reg_dispose(t);
        break;
    }
}

int energy_plan_store_register_city(const char* city, const char* price,
                                    double lat, double lon, void* context,
                                    EpCityOnDone on_done) {
    if (!city || !price || !on_done || !g_store.initialised) {
        return -1;
    }

    RegTask* t = calloc(1, sizeof(*t));
    if (!t) {
        return -1;
    }

    t->cities = calloc((size_t)g_store.max_cities + 1, sizeof(EpCityEntry));
    if (!t->cities) {
        free(t);
        return -1;
    }

    strncpy(t->city, city, sizeof(t->city) - 1);
    strncpy(t->price, price, sizeof(t->price) - 1);
    t->lat     = lat;
    t->lon     = lon;
    t->fd      = -1;
    t->cb_ctx  = context;
    t->on_done = on_done;
    t->state   = REG_INIT;

    t->task = smw_create_task(t, ep_registry_task_work);
    if (!t->task) {
        free(t->cities);
        free(t);
        return -1;
    }
    return 0;
}

EpCityList energy_plan_store_load_cities(void) {
    EpCityList result = {NULL, 0};
    if (!g_store.initialised) {
        return result;
    }

    FILE* fp = fopen(g_store.cities_csv, "r");
    if (!fp) {
        if (errno != ENOENT) {
            LOG_WARN(LOG_MOD, "fopen(%s): %s", g_store.cities_csv,
                     strerror(errno));
        }
        return result;
    }

    if (flock(fileno(fp), LOCK_SH) != 0) {
        LOG_WARN(LOG_MOD, "flock(SH): %s", strerror(errno));
        fclose(fp);
        return result;
    }

    EpCityEntry* entries =
        calloc((size_t)g_store.max_cities, sizeof(EpCityEntry));
    if (!entries) {
        flock(fileno(fp), LOCK_UN);
        fclose(fp);
        return result;
    }

    time_t now   = time(NULL);
    int    count = 0;
    char   line[512];

    while (fgets(line, sizeof(line), fp) && count < g_store.max_cities) {
        char* sp  = NULL;
        char* tok = strtok_r(line, ",", &sp);
        if (!tok || tok[0] == '\0') {
            continue;
        }

        EpCityEntry e = {0};
        strncpy(e.city, tok, sizeof(e.city) - 1);

        tok = strtok_r(NULL, ",", &sp);
        strncpy(e.price, tok ? tok : "", sizeof(e.price) - 1);

        tok   = strtok_r(NULL, ",", &sp);
        e.lat = tok ? atof(tok) : 0.0;

        tok   = strtok_r(NULL, ",", &sp);
        e.lon = tok ? atof(tok) : 0.0;

        tok             = strtok_r(NULL, ",", &sp);
        e.last_accessed = tok ? (time_t)atoll(tok) : now;

        if (now - e.last_accessed > (time_t)g_store.city_ttl_seconds) {
            continue;
        }

        entries[count++] = e;
    }

    flock(fileno(fp), LOCK_UN);
    fclose(fp);

    result.entries = entries;
    result.count   = count;
    return result;
}

int energy_plan_store_get_weather_path(const char* city, double lat, double lon,
                                       char* out, size_t out_size) {
    if (!g_store.initialised || !city || !out) {
        return -1;
    }
    char lower[256];
    ep_to_lower(city, lower, sizeof(lower));
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

int energy_plan_store_save_weather(const char* city, double lat, double lon,
                                   json_t* data) {
    if (!g_store.initialised || !city || !data) {
        return -1;
    }
    char path[512];
    if (energy_plan_store_get_weather_path(city, lat, lon, path,
                                           sizeof(path)) != 0) {
        LOG_WARN(LOG_MOD, "get_weather_path failed for '%s'", city);
        return -1;
    }
    LOG_INFO(LOG_MOD, "Saving weather for %s -> %s", city, path);
    return ep_write_json_atomic(path, data);
}

int energy_plan_store_save_elpris(json_t* merged_array) {
    if (!g_store.initialised || !merged_array) {
        return -1;
    }
    LOG_INFO(LOG_MOD, "Saving merged elpris -> %s", g_store.elpris_json);
    return ep_write_json_atomic(g_store.elpris_json, merged_array);
}

int energy_plan_store_acquire_write_lock(void) {
    if (!g_store.initialised) {
        return -1;
    }

    int fd = open(g_store.output_lock, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        LOG_WARN(LOG_MOD, "open lock(%s): %s", g_store.output_lock,
                 strerror(errno));
        return -1;
    }
    LOG_DEBUG(LOG_MOD, "Waiting for exclusive output lock…");
    if (flock(fd, LOCK_EX) != 0) {
        LOG_WARN(LOG_MOD, "flock(EX): %s", strerror(errno));
        close(fd);
        return -1;
    }
    LOG_DEBUG(LOG_MOD, "Exclusive output lock acquired");
    return fd;
}

void energy_plan_store_release_write_lock(int lock_handle) {
    if (lock_handle < 0) {
        return;
    }
    flock(lock_handle, LOCK_UN);
    close(lock_handle);
    LOG_DEBUG(LOG_MOD, "Exclusive output lock released");
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
        } else {
            LOG_DEBUG(LOG_MOD, "Cleared: %s", path);
        }
    }
    closedir(d);
    return rc;
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

int energy_plan_store_write_output(const char* city, const char* zone,
                                   json_t* data) {
    if (!g_store.initialised || !city || !zone || !data) {
        return -1;
    }

    char path[512];
    if (energy_plan_store_get_output_path(city, zone, path, sizeof(path)) !=
        0) {
        LOG_WARN(LOG_MOD, "get_output_path failed for '%s-%s'", city, zone);
        return -1;
    }

    char tmp[640];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    if (json_dump_file(data, tmp, JSON_INDENT(2)) != 0) {
        LOG_WARN(LOG_MOD, "json_dump_file(%s) failed", tmp);
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        LOG_WARN(LOG_MOD, "rename(%s -> %s): %s", tmp, path, strerror(errno));
        unlink(tmp);
        return -1;
    }

    LOG_INFO(LOG_MOD, "Output written: %s", path);
    return 0;
}

#define OUT_CHUNK 4096

typedef enum {
    OUT_INIT,
    OUT_LOCK,
    OUT_OPEN,
    OUT_READ,
    OUT_DONE,
    OUT_ERROR,
    OUT_DISPOSE,
} OutState;

typedef struct {
    void* task;

    int lock_fd;
    int file_fd;

    char file_path[512];

    char*  rbuf;
    size_t rbuf_len;
    size_t rbuf_cap;

    EpOutputStatus result;
    OutState       state;

    void*          cb_ctx;
    EpOutputOnDone on_done;
} OutTask;

static void out_lock(OutTask* t) {
    if (t->lock_fd < 0) {
        t->lock_fd =
            open(g_store.output_lock, O_RDONLY | O_CREAT | O_NONBLOCK, 0644);
        if (t->lock_fd < 0) {
            LOG_WARN(LOG_MOD, "open lock(%s): %s", g_store.output_lock,
                     strerror(errno));
            t->result = EP_OUTPUT_LOCK_ERROR;
            t->state  = OUT_ERROR;
            return;
        }
    }
    if (flock(t->lock_fd, LOCK_SH | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            return; // retry next ticl
        }
        LOG_WARN(LOG_MOD, "flock(SH): %s", strerror(errno));
        t->result = EP_OUTPUT_LOCK_ERROR;
        t->state  = OUT_ERROR;
        return;
    }
    t->state = OUT_OPEN;
}

static void out_open(OutTask* t) {
    t->file_fd = open(t->file_path, O_RDONLY | O_NONBLOCK);
    if (t->file_fd < 0) {
        t->result =
            (errno == ENOENT) ? EP_OUTPUT_NOT_FOUND : EP_OUTPUT_READ_ERROR;
        LOG_WARN(LOG_MOD, "open(%s): %s", t->file_path, strerror(errno));
        t->state = OUT_ERROR;
        return;
    }
    t->state = OUT_READ;
}

static void out_read(OutTask* t) {
    char    chunk[OUT_CHUNK];
    ssize_t n = read(t->file_fd, chunk, sizeof(chunk));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        LOG_WARN(LOG_MOD, "read(%s): %s", t->file_path, strerror(errno));
        t->result = EP_OUTPUT_READ_ERROR;
        t->state  = OUT_ERROR;
        return;
    }
    if (n > 0) {
        size_t need = t->rbuf_len + (size_t)n;
        if (need > t->rbuf_cap) {
            size_t nc = need + OUT_CHUNK;
            char*  nb = realloc(t->rbuf, nc + 1); /* +1 for NUL */
            if (!nb) {
                t->result = EP_OUTPUT_READ_ERROR;
                t->state  = OUT_ERROR;
                return;
            }
            t->rbuf     = nb;
            t->rbuf_cap = nc;
        }
        memcpy(t->rbuf + t->rbuf_len, chunk, (size_t)n);
        t->rbuf_len += (size_t)n;
        return;
    }
    /* EOF */
    t->rbuf[t->rbuf_len] = '\0';
    flock(t->lock_fd, LOCK_UN);
    close(t->lock_fd);
    t->lock_fd = -1;
    close(t->file_fd);
    t->file_fd = -1;
    t->result  = EP_OUTPUT_OK;
    t->state   = OUT_DONE;
}

static void out_dispose(OutTask* t) {
    if (t->task) {
        smw_destroy_task(t->task);
        t->task = NULL;
    }
    if (t->lock_fd >= 0) {
        flock(t->lock_fd, LOCK_UN);
        close(t->lock_fd);
        t->lock_fd = -1;
    }
    if (t->file_fd >= 0) {
        close(t->file_fd);
        t->file_fd = -1;
    }
    free(t->rbuf);
    memset(t, 0, sizeof(*t));
    free(t);
}

void ep_output_reader_task_work(void* context, uint64_t mon_time) {
    (void)mon_time;
    OutTask* t = (OutTask*)context;
    switch (t->state) {
    case OUT_INIT:
        t->state = OUT_LOCK;
        break;
    case OUT_LOCK:
        out_lock(t);
        break;
    case OUT_OPEN:
        out_open(t);
        break;
    case OUT_READ:
        out_read(t);
        break;
    case OUT_DONE: {
        char*  buf  = t->rbuf;
        size_t len  = t->rbuf_len;
        t->rbuf     = NULL;
        t->rbuf_len = 0;
        t->on_done(t->cb_ctx, EP_OUTPUT_OK, buf, len);
        t->state = OUT_DISPOSE;
        break;
    }
    case OUT_ERROR:
        if (t->lock_fd >= 0) {
            flock(t->lock_fd, LOCK_UN);
            close(t->lock_fd);
            t->lock_fd = -1;
        }
        if (t->file_fd >= 0) {
            close(t->file_fd);
            t->file_fd = -1;
        }
        t->on_done(t->cb_ctx, t->result, NULL, 0);
        t->state = OUT_DISPOSE;
        break;
    case OUT_DISPOSE:
        out_dispose(t);
        break;
    }
}

int energy_plan_store_read_output_async(const char* city, const char* price,
                                        void* context, EpOutputOnDone on_done) {
    if (!city || !price || !on_done || !g_store.initialised) {
        return -1;
    }

    OutTask* t = calloc(1, sizeof(*t));
    if (!t) {
        return -1;
    }

    if (energy_plan_store_get_output_path(city, price, t->file_path,
                                          sizeof(t->file_path)) != 0) {
        free(t);
        return -1;
    }

    t->lock_fd = -1;
    t->file_fd = -1;
    t->cb_ctx  = context;
    t->on_done = on_done;
    t->state   = OUT_INIT;

    t->task = smw_create_task(t, ep_output_reader_task_work);
    if (!t->task) {
        free(t);
        return -1;
    }
    return 0;
}

#define RUN_SLOTS_PER_DAY_MAX 96 // upper bound for stack allocation
#define RUN_NUM_ZONES 4

typedef struct {
    double sek_per_kwh;
    char   time_start[40];
    char   time_end[40];
} RunPrice;

typedef struct {
    RunPrice zones[RUN_NUM_ZONES][RUN_SLOTS_PER_DAY_MAX];
} RunElpris;

static int run_zone_idx(const char* s) {
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
    return 2; // default SE3
}

static int run_load_elpris(RunElpris* out, int slots_per_day) {
    char path[512];
    if (energy_plan_store_get_elpris_path(path, sizeof(path)) != 0) {
        LOG_WARN(LOG_MOD, "run_compute: get_elpris_path failed");
        return -1;
    }

    json_error_t err;
    json_t*      root = json_load_file(path, 0, &err);
    if (!root) {
        LOG_WARN(LOG_MOD, "run_compute: cannot load elpris '%s': %s", path,
                 err.text);
        return -1;
    }
    if (!json_is_array(root)) {
        LOG_WARN(LOG_MOD, "run_compute: elpris root is not array");
        json_decref(root);
        return -1;
    }

    int    expected = RUN_NUM_ZONES * slots_per_day;
    size_t total    = json_array_size(root);

    if ((int)total < expected) {
        LOG_WARN(LOG_MOD, "run_compute: elpris expected %d entries, got %zu",
                 expected, total);
    }

    for (int i = 0; i < expected && i < (int)total; i++) {
        json_t*   obj  = json_array_get(root, i);
        int       zone = i / slots_per_day;
        int       sl   = i % slots_per_day;
        RunPrice* pp   = &out->zones[zone][sl];
        memset(pp, 0, sizeof(*pp));

        json_t* sek     = json_object_get(obj, "SEK_per_kWh");
        pp->sek_per_kwh = sek ? json_number_value(sek) : 0.0;

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
    LOG_INFO(LOG_MOD, "run_compute: loaded %zu elpris entries", total);
    return (int)total;
}

static json_t* run_load_weather(const char* city, double lat, double lon,
                                int slots_per_day, int* weather_count) {
    char path[512];
    if (energy_plan_store_get_weather_path(city, lat, lon, path,
                                           sizeof(path)) != 0) {
        LOG_WARN(LOG_MOD, "run_compute: weather path too long for '%s'", city);
        return NULL;
    }

    json_error_t err;
    json_t*      root = json_load_file(path, 0, &err);
    if (!root) {
        LOG_WARN(LOG_MOD, "run_compute: cannot load weather for '%s': %s", city,
                 err.text);
        return NULL;
    }

    json_t* forecast =
        json_object_get(json_object_get(root, "data"), "minutely_forecast");
    if (!json_is_array(forecast)) {
        LOG_WARN(LOG_MOD, "run_compute: no minutely_forecast array for '%s'",
                 city);
        json_decref(root);
        return NULL;
    }

    json_t* result = json_array();
    int     count  = 0;
    size_t  idx;
    json_t* slot;

    json_array_foreach(forecast, idx, slot) {
        if (count >= slots_per_day) {
            break;
        }
        json_array_append(result, slot);
        count++;
    }

    json_decref(root);
    *weather_count = count;
    return result;
}

static json_t* run_build_prices_array(const RunElpris* elpris, int zone_idx,
                                      int slots_per_day) {
    json_t* arr = json_array();
    for (int i = 0; i < slots_per_day; i++) {
        const RunPrice* pp = &elpris->zones[zone_idx][i];
        json_t*         s  = json_object();
        json_object_set_new(s, "SEK_per_kWh", json_real(pp->sek_per_kwh));
        json_object_set_new(s, "time_start", json_string(pp->time_start));
        json_object_set_new(s, "time_end", json_string(pp->time_end));
        json_array_append_new(arr, s);
    }
    return arr;
}

static int run_write_output(const char* city, const char* zone_str, double lat,
                            double lon, json_t* weather, int weather_count,
                            json_t* prices, const bool* decisions,
                            int slots_per_day) {
    int buy = 0;
    for (int i = 0; i < slots_per_day; i++) {
        buy += decisions[i] ? 1 : 0;
    }

    json_t* root = json_object();
    json_object_set_new(root, "city", json_string(city));
    json_object_set_new(root, "latitude", json_real(lat));
    json_object_set_new(root, "longitude", json_real(lon));
    json_object_set_new(root, "price_zone", json_string(zone_str));
    json_object_set_new(root, "slots_total", json_integer(slots_per_day));
    json_object_set_new(root, "slots_buy", json_integer(buy));
    json_object_set_new(root, "slots_sell", json_integer(slots_per_day - buy));

    json_t* arr = json_array();
    for (int i = 0; i < slots_per_day; i++) {
        json_t* w_slot =
            (i < weather_count) ? json_array_get(weather, (size_t)i) : NULL;
        json_t* p_slot = json_array_get(prices, (size_t)i);
        json_t* s      = json_object();

        json_object_set_new(s, "slot", json_integer(i));
        json_object_set_new(
            s, "time",
            json_string(w_slot
                            ? json_string_value(json_object_get(w_slot, "time"))
                            : "N/A"));
        json_object_set_new(
            s, "temperature",
            json_real(w_slot ? json_number_value(
                                   json_object_get(w_slot, "temperature"))
                             : 0.0));
        json_object_set_new(
            s, "price_sek",
            json_real(p_slot ? json_number_value(
                                   json_object_get(p_slot, "SEK_per_kWh"))
                             : 0.0));
        json_object_set_new(s, "action",
                            json_string(decisions[i] ? "buy" : "sell"));
        json_array_append_new(arr, s);
    }
    json_object_set_new(root, "decisions", arr);

    int rc = energy_plan_store_write_output(city, zone_str, root);
    json_decref(root);
    return rc;
}

int energy_plan_store_run_compute(int           slots_per_day,
                                  EpAlgorithmFn algorithm_fn) {
    if (!g_store.initialised || !algorithm_fn || slots_per_day <= 0 ||
        slots_per_day > RUN_SLOTS_PER_DAY_MAX) {
        LOG_WARN(LOG_MOD,
                 "run_compute: bad arguments or store not initialised");
        return -1;
    }

    int lock = energy_plan_store_acquire_write_lock();
    if (lock < 0) {
        LOG_WARN(LOG_MOD, "run_compute: could not acquire write lock");
        return -1;
    }

    int ret = 0;

    EpCityList cities = energy_plan_store_load_cities();
    if (!cities.entries || cities.count == 0) {
        LOG_WARN(LOG_MOD, "run_compute: no cities in registry");
        ret = -1;
        goto done;
    }

    RunElpris elpris;
    memset(&elpris, 0, sizeof(elpris));
    if (run_load_elpris(&elpris, slots_per_day) <= 0) {
        LOG_WARN(LOG_MOD, "run_compute: failed to load elpris");
        free(cities.entries);
        ret = -1;
        goto done;
    }

    LOG_INFO(LOG_MOD, "run_compute: clearing old outputs…");
    if (energy_plan_store_clear_outputs() != 0) {
        LOG_WARN(LOG_MOD, "run_compute: some old outputs could not be deleted");
    }

    {
        int ok = 0, fail = 0;

        for (int i = 0; i < cities.count; i++) {
            EpCityEntry* e = &cities.entries[i];
            LOG_INFO(LOG_MOD, "run_compute: --- '%s' (%s) ---", e->city,
                     e->price);

            int     weather_count = 0;
            json_t* weather       = run_load_weather(e->city, e->lat, e->lon,
                                                     slots_per_day, &weather_count);
            if (!weather) {
                LOG_WARN(LOG_MOD,
                         "run_compute: skipping '%s' – weather unavailable",
                         e->city);
                fail++;
                continue;
            }
            if (weather_count < slots_per_day) {
                LOG_WARN(LOG_MOD, "run_compute: '%s' only %d/%d weather slots",
                         e->city, weather_count, slots_per_day);
            }

            int     zone_idx = run_zone_idx(e->price);
            json_t* prices =
                run_build_prices_array(&elpris, zone_idx, slots_per_day);

            bool* decisions = calloc((size_t)slots_per_day, sizeof(bool));
            if (!decisions) {
                LOG_WARN(LOG_MOD, "run_compute: alloc failed for '%s'",
                         e->city);
                json_decref(weather);
                json_decref(prices);
                fail++;
                continue;
            }

            algorithm_fn(e->city, e->price, weather, weather_count, prices,
                         slots_per_day, decisions);

            int rc = run_write_output(e->city, e->price, e->lat, e->lon,
                                      weather, weather_count, prices, decisions,
                                      slots_per_day);

            free(decisions);
            json_decref(weather);
            json_decref(prices);

            if (rc != 0) {
                LOG_WARN(LOG_MOD, "run_compute: write failed for '%s'",
                         e->city);
                fail++;
            } else {
                ok++;
            }
        }

        free(cities.entries);

        if (fail > 0) {
            LOG_WARN(LOG_MOD, "run_compute: %d ok, %d failed", ok, fail);
            ret = 1;
        } else {
            LOG_INFO(LOG_MOD, "run_compute: all %d cities processed", ok);
            ret = 0;
        }
    }

done:
    energy_plan_store_release_write_lock(lock);
    return ret;
}
