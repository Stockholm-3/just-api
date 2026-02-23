#include "city_registry.h"

#include "logger/logger.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <smw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define READ_CHUNK_SIZE 4096
#define WRITE_CHUNK_SIZE 4096

static int  ensure_path_for_file(const char* filepath);
static int  open_or_create_nonblock(const char* filepath);
static void process_csv(CityRegistry* reg);
static int  build_write_buffer(CityRegistry* reg);

static int ensure_path_for_file(const char* filepath) {
    if (!filepath) {
        return -1;
    }
    char path[512];
    strncpy(path, filepath, sizeof(path));
    path[sizeof(path) - 1] = '\0';
    char*       dir        = dirname(path);
    struct stat st         = {0};
    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0755) != 0) {
            LOG_WARN("CITY_REG", "Failed to create directory: %s", dir);
            return -1;
        }
    }
    return 0;
}

static int open_or_create_nonblock(const char* filepath) {
    int fd = open(filepath, O_RDWR | O_NONBLOCK);
    if (fd >= 0) {
        return fd;
    }
    if (errno == ENOENT) {
        fd = open(filepath, O_RDWR | O_CREAT | O_NONBLOCK, 0644);
    }
    return fd;
}

static void process_csv(CityRegistry* reg) {
    reg->city_count    = 0;
    int    exists_flag = 0;
    time_t now         = time(NULL);

    char* cursor = reg->read_buf;
    char* end    = reg->read_buf + reg->read_buf_size;

    while (cursor < end && reg->city_count < MAX_REGISTERED_CITIES) {
        char*  newline = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_len =
            newline ? (size_t)(newline - cursor) : (size_t)(end - cursor);
        if (line_len == 0) {
            cursor = newline ? newline + 1 : end;
            continue;
        }

        char line[512];
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';
        cursor         = newline ? newline + 1 : end;

        char* saveptr = NULL;
        int   idx     = reg->city_count;
        char* token;

        token = strtok_r(line, ",", &saveptr);
        if (!token) {
            continue;
        }
        strncpy(reg->cities[idx].city, token, sizeof(reg->cities[idx].city));
        reg->cities[idx].city[sizeof(reg->cities[idx].city) - 1] = '\0';

        token = strtok_r(NULL, ",", &saveptr);
        strncpy(reg->cities[idx].price, token ? token : "",
                sizeof(reg->cities[idx].price));
        reg->cities[idx].price[sizeof(reg->cities[idx].price) - 1] = '\0';

        token                = strtok_r(NULL, ",", &saveptr);
        reg->cities[idx].lat = token ? atof(token) : 0.0;

        token                = strtok_r(NULL, ",", &saveptr);
        reg->cities[idx].lon = token ? atof(token) : 0.0;

        token                          = strtok_r(NULL, ",", &saveptr);
        reg->cities[idx].last_accessed = token ? (time_t)atoll(token) : now;

        if (strcmp(reg->cities[idx].city, reg->city) == 0) {
            exists_flag                    = 1;
            reg->cities[idx].last_accessed = now;
        }

        if (now - reg->cities[idx].last_accessed > (time_t)CITY_TTL_SECONDS) {
            reg->cities[idx].city[0] = '\0';
        }

        reg->city_count++;
    }

    if (exists_flag) {
        reg->result = CITY_EXISTS;
        return;
    }

    int valid_count = 0;
    for (int i = 0; i < reg->city_count; i++) {
        if (reg->cities[i].city[0] != '\0') {
            valid_count++;
        }
    }

    if (valid_count >= MAX_REGISTERED_CITIES) {
        reg->result = CITY_LIMIT_REACHED;
        return;
    }

    int idx = reg->city_count;
    strncpy(reg->cities[idx].city, reg->city, sizeof(reg->cities[idx].city));
    reg->cities[idx].city[sizeof(reg->cities[idx].city) - 1] = '\0';
    strncpy(reg->cities[idx].price, reg->price, sizeof(reg->cities[idx].price));
    reg->cities[idx].price[sizeof(reg->cities[idx].price) - 1] = '\0';
    reg->cities[idx].lat                                       = reg->lat;
    reg->cities[idx].lon                                       = reg->lon;
    reg->cities[idx].last_accessed                             = now;
    reg->city_count++;
    reg->result = CITY_ADDED;
}

static int build_write_buffer(CityRegistry* reg) {
    size_t cap = (size_t)(reg->city_count + 1) * 512;
    char*  buf = malloc(cap);
    if (!buf) {
        return -1;
    }
    size_t offset = 0;
    for (int i = 0; i < reg->city_count; i++) {
        if (reg->cities[i].city[0] == '\0') {
            continue;
        }
        int written = snprintf(
            buf + offset, cap - offset, "%s,%s,%.6f,%.6f,%ld\n",
            reg->cities[i].city, reg->cities[i].price, reg->cities[i].lat,
            reg->cities[i].lon, (long)reg->cities[i].last_accessed);
        if (written < 0 || (size_t)written >= cap - offset) {
            free(buf);
            return -1;
        }
        offset += (size_t)written;
    }
    reg->write_buf      = buf;
    reg->write_buf_size = offset;
    reg->write_offset   = 0;
    return 0;
}

static void state_open(CityRegistry* reg) {
    if (ensure_path_for_file(reg->filepath) != 0) {
        reg->state = CITY_REGISTRY_STATE_ERROR;
        return;
    }
    int fd = open_or_create_nonblock(reg->filepath);
    if (fd < 0) {
        LOG_WARN("CITY_REG", "Failed to open file: %s (errno %d)",
                 reg->filepath, errno);
        reg->state = CITY_REGISTRY_STATE_ERROR;
        return;
    }
    reg->fd    = fd;
    reg->state = CITY_REGISTRY_STATE_LOCK;
}

static void state_lock(CityRegistry* reg) {
    if (flock(reg->fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            return;
        }
        LOG_WARN("CITY_REG", "Failed to lock file (errno %d)", errno);
        reg->state = CITY_REGISTRY_STATE_ERROR;
        return;
    }
    reg->state = CITY_REGISTRY_STATE_READ;
}

static void state_read(CityRegistry* reg) {
    char    chunk[READ_CHUNK_SIZE];
    ssize_t bytes = read(reg->fd, chunk, sizeof(chunk));
    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        LOG_WARN("CITY_REG", "Read error (errno %d)", errno);
        reg->state = CITY_REGISTRY_STATE_ERROR;
        return;
    }
    if (bytes > 0) {
        size_t new_size = reg->read_buf_size + (size_t)bytes;
        if (new_size > reg->read_buf_cap) {
            size_t new_cap = new_size + READ_CHUNK_SIZE;
            char*  new_buf = realloc(reg->read_buf, new_cap);
            if (!new_buf) {
                reg->state = CITY_REGISTRY_STATE_ERROR;
                return;
            }
            reg->read_buf     = new_buf;
            reg->read_buf_cap = new_cap;
        }
        memcpy(reg->read_buf + reg->read_buf_size, chunk, (size_t)bytes);
        reg->read_buf_size += (size_t)bytes;
        return;
    }
    reg->state = CITY_REGISTRY_STATE_PROCESS;
}

static void state_process(CityRegistry* reg) {
    process_csv(reg);
    if (reg->result == CITY_LIMIT_REACHED) {
        reg->state = CITY_REGISTRY_STATE_DONE;
        return;
    }
    if (build_write_buffer(reg) != 0) {
        reg->state = CITY_REGISTRY_STATE_ERROR;
        return;
    }
    reg->state = CITY_REGISTRY_STATE_SEEK;
}

static void state_seek(CityRegistry* reg) {
    if (lseek(reg->fd, 0, SEEK_SET) != 0) {
        LOG_WARN("CITY_REG", "lseek failed (errno %d)", errno);
        reg->state = CITY_REGISTRY_STATE_ERROR;
        return;
    }
    if (ftruncate(reg->fd, 0) != 0) {
        LOG_WARN("CITY_REG", "ftruncate failed (errno %d)", errno);
        reg->state = CITY_REGISTRY_STATE_ERROR;
        return;
    }
    reg->state = CITY_REGISTRY_STATE_WRITE;
}

static void state_write(CityRegistry* reg) {
    while (reg->write_offset < reg->write_buf_size) {
        ssize_t sent = write(reg->fd, reg->write_buf + reg->write_offset,
                             reg->write_buf_size - reg->write_offset);
        if (sent > 0) {
            reg->write_offset += (size_t)sent;
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            LOG_WARN("CITY_REG", "Write error (errno %d)", errno);
            reg->state = CITY_REGISTRY_STATE_ERROR;
            return;
        }
    }
    reg->state = CITY_REGISTRY_STATE_DONE;
}

void city_registry_task_work(void* context, uint64_t mon_time) {
    (void)mon_time;
    CityRegistry* reg = (CityRegistry*)context;
    switch (reg->state) {
    case CITY_REGISTRY_STATE_INIT:
        reg->state = CITY_REGISTRY_STATE_OPEN;
        break;
    case CITY_REGISTRY_STATE_OPEN:
        state_open(reg);
        break;
    case CITY_REGISTRY_STATE_LOCK:
        state_lock(reg);
        break;
    case CITY_REGISTRY_STATE_READ:
        state_read(reg);
        break;
    case CITY_REGISTRY_STATE_PROCESS:
        state_process(reg);
        break;
    case CITY_REGISTRY_STATE_SEEK:
        state_seek(reg);
        break;
    case CITY_REGISTRY_STATE_WRITE:
        state_write(reg);
        break;
    case CITY_REGISTRY_STATE_DONE:
        reg->on_done(reg->callback_context, reg->result);
        reg->state = CITY_REGISTRY_STATE_DISPOSE;
        break;
    case CITY_REGISTRY_STATE_ERROR:
        LOG_WARN("CITY_REG", "Operation failed for city: %s", reg->city);
        reg->on_done(reg->callback_context, CITY_LIMIT_REACHED);
        reg->state = CITY_REGISTRY_STATE_DISPOSE;
        break;
    case CITY_REGISTRY_STATE_DISPOSE:
        city_registry_dispose(reg);
        break;
    }
}

int city_registry_initiate(const char* filepath, const char* city,
                           const char* price, double lat, double lon,
                           void* context, CityRegistryOnDone on_done) {
    if (!filepath || !city || !price || !on_done) {
        return -1;
    }
    CityRegistry* reg = calloc(1, sizeof(CityRegistry));
    if (!reg) {
        return -1;
    }
    strncpy(reg->filepath, filepath, sizeof(reg->filepath));
    reg->filepath[sizeof(reg->filepath) - 1] = '\0';
    strncpy(reg->city, city, sizeof(reg->city));
    reg->city[sizeof(reg->city) - 1] = '\0';
    strncpy(reg->price, price, sizeof(reg->price));
    reg->price[sizeof(reg->price) - 1] = '\0';
    reg->lat                           = lat;
    reg->lon                           = lon;
    reg->fd                            = -1;
    reg->callback_context              = context;
    reg->on_done                       = on_done;
    reg->state                         = CITY_REGISTRY_STATE_INIT;
    reg->task = smw_create_task(reg, city_registry_task_work);
    if (!reg->task) {
        free(reg);
        return -1;
    }
    return 0;
}

CityLoadResult city_registry_load_all(const char* filepath) {
    CityLoadResult result = {NULL, 0};
    if (!filepath) {
        return result;
    }
    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        if (errno != ENOENT) {
            LOG_WARN("CITY_REG", "Failed to open registry: %s (errno %d)",
                     filepath, errno);
        }
        return result;
    }
    if (flock(fileno(fp), LOCK_SH) != 0) {
        LOG_WARN("CITY_REG", "Failed to acquire shared lock (errno %d)", errno);
        fclose(fp);
        return result;
    }
    CityEntry* entries = malloc(sizeof(CityEntry) * MAX_REGISTERED_CITIES);
    if (!entries) {
        LOG_WARN("CITY_REG", "Allocation failed for city load buffer");
        flock(fileno(fp), LOCK_UN);
        fclose(fp);
        return result;
    }
    time_t now   = time(NULL);
    int    count = 0;
    char   line[512];
    while (fgets(line, sizeof(line), fp) && count < MAX_REGISTERED_CITIES) {
        char* saveptr = NULL;
        char* token   = strtok_r(line, ",", &saveptr);
        if (!token || token[0] == '\0') {
            continue;
        }
        CityEntry entry = {0};
        strncpy(entry.city, token, sizeof(entry.city));
        entry.city[sizeof(entry.city) - 1] = '\0';
        token                              = strtok_r(NULL, ",", &saveptr);
        strncpy(entry.price, token ? token : "", sizeof(entry.price));
        entry.price[sizeof(entry.price) - 1] = '\0';
        token                                = strtok_r(NULL, ",", &saveptr);
        entry.lat                            = token ? atof(token) : 0.0;
        token                                = strtok_r(NULL, ",", &saveptr);
        entry.lon                            = token ? atof(token) : 0.0;
        token                                = strtok_r(NULL, ",", &saveptr);
        entry.last_accessed = token ? (time_t)atoll(token) : now;
        if (now - entry.last_accessed > (time_t)CITY_TTL_SECONDS) {
            continue;
        }
        entries[count++] = entry;
    }
    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    result.entries = entries;
    result.count   = count;
    return result;
}

void city_registry_dispose(CityRegistry* reg) {
    if (!reg) {
        return;
    }
    if (reg->task) {
        smw_destroy_task(reg->task);
        reg->task = NULL;
    }
    if (reg->fd >= 0) {
        flock(reg->fd, LOCK_UN);
        close(reg->fd);
        reg->fd = -1;
    }
    free(reg->read_buf);
    free(reg->write_buf);
    memset(reg, 0, sizeof(*reg));
    free(reg);
}

static void output_state_lock(CityOutputReader* r) {
    /*
     * Open the shared lock file.  O_CREAT means we don't fail if the parser
     * has never run; we'll catch the missing output file in STATE_OPEN.
     */
    if (r->lock_fd < 0) {
        r->lock_fd =
            open(COMPUTE_OUTPUT_LOCK, O_RDONLY | O_CREAT | O_NONBLOCK, 0644);
        if (r->lock_fd < 0) {
            LOG_WARN("CITY_OUTPUT", "Cannot open lock file '%s' (errno %d)",
                     COMPUTE_OUTPUT_LOCK, errno);
            r->result = CITY_OUTPUT_LOCK_ERROR;
            r->state  = CITY_OUTPUT_STATE_ERROR;
            return;
        }
    }

    /* Non-blocking shared lock – retry next tick if parser holds exclusive. */
    if (flock(r->lock_fd, LOCK_SH | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            return; /* stay in LOCK state */
        }
        LOG_WARN("CITY_OUTPUT", "flock(LOCK_SH) failed (errno %d)", errno);
        r->result = CITY_OUTPUT_LOCK_ERROR;
        r->state  = CITY_OUTPUT_STATE_ERROR;
        return;
    }

    r->state = CITY_OUTPUT_STATE_OPEN;
}

static void output_state_open(CityOutputReader* r) {
    r->file_fd = open(r->file_path, O_RDONLY | O_NONBLOCK);
    if (r->file_fd < 0) {
        if (errno == ENOENT) {
            LOG_WARN("CITY_OUTPUT", "Compute output not found: %s",
                     r->file_path);
            r->result = CITY_OUTPUT_NOT_FOUND;
        } else {
            LOG_WARN("CITY_OUTPUT", "Cannot open '%s' (errno %d)", r->file_path,
                     errno);
            r->result = CITY_OUTPUT_READ_ERROR;
        }
        r->state = CITY_OUTPUT_STATE_ERROR;
        return;
    }
    r->state = CITY_OUTPUT_STATE_READ;
}

static void output_state_read(CityOutputReader* r) {
    char    chunk[READ_CHUNK_SIZE];
    ssize_t bytes = read(r->file_fd, chunk, sizeof(chunk));

    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return; /* retry next tick */
        }
        LOG_WARN("CITY_OUTPUT", "Read error on '%s' (errno %d)", r->file_path,
                 errno);
        r->result = CITY_OUTPUT_READ_ERROR;
        r->state  = CITY_OUTPUT_STATE_ERROR;
        return;
    }

    if (bytes > 0) {
        size_t new_size = r->read_buf_size + (size_t)bytes;
        if (new_size > r->read_buf_cap) {
            size_t new_cap = new_size + READ_CHUNK_SIZE;
            char*  new_buf = realloc(r->read_buf, new_cap + 1); /* +1 for NUL */
            if (!new_buf) {
                r->result = CITY_OUTPUT_READ_ERROR;
                r->state  = CITY_OUTPUT_STATE_ERROR;
                return;
            }
            r->read_buf     = new_buf;
            r->read_buf_cap = new_cap;
        }
        memcpy(r->read_buf + r->read_buf_size, chunk, (size_t)bytes);
        r->read_buf_size += (size_t)bytes;
        return; /* keep reading */
    }

    /* EOF – NUL-terminate so the buffer is a valid C string */
    r->read_buf[r->read_buf_size] = '\0';

    /* We have everything we need: release the lock immediately. */
    flock(r->lock_fd, LOCK_UN);
    close(r->lock_fd);
    r->lock_fd = -1;
    close(r->file_fd);
    r->file_fd = -1;

    r->result = CITY_OUTPUT_OK;
    r->state  = CITY_OUTPUT_STATE_DONE;
}

/* ── task work function ─────────────────────────────────────────────────── */

void city_output_reader_task_work(void* context, uint64_t mon_time) {
    (void)mon_time;
    CityOutputReader* r = (CityOutputReader*)context;

    switch (r->state) {
    case CITY_OUTPUT_STATE_INIT:
        r->state = CITY_OUTPUT_STATE_LOCK;
        break;

    case CITY_OUTPUT_STATE_LOCK:
        output_state_lock(r);
        break;

    case CITY_OUTPUT_STATE_OPEN:
        output_state_open(r);
        break;

    case CITY_OUTPUT_STATE_READ:
        output_state_read(r);
        break;

    case CITY_OUTPUT_STATE_DONE:
        /*
         * Transfer buffer ownership to the callback.
         * Set read_buf to NULL so dispose() doesn't double-free it.
         */
        {
            char*  buf       = r->read_buf;
            size_t len       = r->read_buf_size;
            r->read_buf      = NULL;
            r->read_buf_size = 0;
            r->on_done(r->callback_context, CITY_OUTPUT_OK, buf, len);
        }
        r->state = CITY_OUTPUT_STATE_DISPOSE;
        break;

    case CITY_OUTPUT_STATE_ERROR:
        /* Release lock/file if still held before firing callback */
        if (r->lock_fd >= 0) {
            flock(r->lock_fd, LOCK_UN);
            close(r->lock_fd);
            r->lock_fd = -1;
        }
        if (r->file_fd >= 0) {
            close(r->file_fd);
            r->file_fd = -1;
        }
        r->on_done(r->callback_context, r->result, NULL, 0);
        r->state = CITY_OUTPUT_STATE_DISPOSE;
        break;

    case CITY_OUTPUT_STATE_DISPOSE:
        city_output_reader_dispose(r);
        break;
    }
}

/* ── public initiate ────────────────────────────────────────────────────── */

int city_output_read_initiate(const char* city, void* context,
                              CityOutputOnDone on_done) {
    if (!city || !on_done) {
        return -1;
    }

    CityOutputReader* r = calloc(1, sizeof(CityOutputReader));
    if (!r) {
        return -1;
    }

    strncpy(r->city, city, sizeof(r->city) - 1);
    r->city[sizeof(r->city) - 1] = '\0';

    snprintf(r->file_path, sizeof(r->file_path), "%s/%s.json",
             COMPUTE_OUTPUT_DIR, city);

    r->lock_fd          = -1;
    r->file_fd          = -1;
    r->callback_context = context;
    r->on_done          = on_done;
    r->state            = CITY_OUTPUT_STATE_INIT;

    r->task = smw_create_task(r, city_output_reader_task_work);
    if (!r->task) {
        free(r);
        return -1;
    }

    return 0;
}

/* ── dispose ────────────────────────────────────────────────────────────── */

void city_output_reader_dispose(CityOutputReader* r) {
    if (!r) {
        return;
    }
    if (r->task) {
        smw_destroy_task(r->task);
        r->task = NULL;
    }
    if (r->lock_fd >= 0) {
        flock(r->lock_fd, LOCK_UN);
        close(r->lock_fd);
        r->lock_fd = -1;
    }
    if (r->file_fd >= 0) {
        close(r->file_fd);
        r->file_fd = -1;
    }
    free(r->read_buf);
    memset(r, 0, sizeof(*r));
    free(r);
}
