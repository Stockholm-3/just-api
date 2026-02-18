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

    char*       dir = dirname(path);
    struct stat st  = {0};
    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0755) != 0) {
            LOG_WARN("CITY_REG", "Failed to create directory: %s", dir);
            return -1;
        }
    }
    return 0;
}

static int open_or_create_nonblock(const char* filepath) {
    // Try to open existing file first
    int fd = open(filepath, O_RDWR | O_NONBLOCK);
    if (fd >= 0) {
        return fd;
    }

    // Create if missing
    if (errno == ENOENT) {
        fd = open(filepath, O_RDWR | O_CREAT | O_NONBLOCK, 0644);
    }

    return fd;
}

static void process_csv(CityRegistry* reg) {
    reg->city_count    = 0;
    int    exists_flag = 0;
    time_t now         = time(NULL);

    // Parse the accumulated read buffer line by line
    char* cursor = reg->read_buf;
    char* end    = reg->read_buf + reg->read_buf_size;

    while (cursor < end && reg->city_count < MAX_REGISTERED_CITIES) {
        // Find line end
        char*  newline = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_len =
            newline ? (size_t)(newline - cursor) : (size_t)(end - cursor);

        if (line_len == 0) {
            cursor = newline ? newline + 1 : end;
            continue;
        }

        // Copy line into a mutable scratch buffer for strtok_r
        char line[512];
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';
        cursor         = newline ? newline + 1 : end;

        char* saveptr = NULL;
        char* token;
        int   idx = reg->city_count;

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

        // Update timestamp on match
        if (strcmp(reg->cities[idx].city, reg->city) == 0) {
            exists_flag                    = 1;
            reg->cities[idx].last_accessed = now;
        }

        // Evict expired entries by blanking their city name
        if (now - reg->cities[idx].last_accessed > (time_t)CITY_TTL_SECONDS) {
            reg->cities[idx].city[0] = '\0';
        }

        reg->city_count++;
    }

    // Determine result and optionally insert new city
    if (exists_flag) {
        reg->result = CITY_EXISTS;
        return;
    }

    // Count live entries
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

    // Append new city
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
    // Estimate: each row is at most ~512 bytes
    size_t cap = (size_t)(reg->city_count + 1) * 512;
    char*  buf = malloc(cap);
    if (!buf) {
        return -1;
    }

    size_t offset = 0;
    for (int i = 0; i < reg->city_count; i++) {
        if (reg->cities[i].city[0] == '\0') {
            continue; // skip evicted / blanked entries
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
    // flock with LOCK_NB returns EWOULDBLOCK if contended; retry next tick
    if (flock(reg->fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            return; // stay in LOCK, try again next tick
        }
        LOG_WARN("CITY_REG", "Failed to lock file (errno %d)", errno);
        reg->state = CITY_REGISTRY_STATE_ERROR;
        return;
    }

    reg->state = CITY_REGISTRY_STATE_READ;
}

static void state_read(CityRegistry* reg) {
    // Grow read buffer on demand and pull in one chunk per tick
    char chunk[READ_CHUNK_SIZE];

    ssize_t bytes = read(reg->fd, chunk, sizeof(chunk));

    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return; // no data yet, try next tick
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
        return; // keep reading on the next tick
    }

    // bytes == 0: EOF
    reg->state = CITY_REGISTRY_STATE_PROCESS;
}

static void state_process(CityRegistry* reg) {
    process_csv(reg);

    if (reg->result == CITY_LIMIT_REACHED) {
        // Nothing to write, skip straight to done
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
                return; // retry next tick
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

    reg->lat              = lat;
    reg->lon              = lon;
    reg->fd               = -1;
    reg->callback_context = context;
    reg->on_done          = on_done;
    reg->state            = CITY_REGISTRY_STATE_INIT;

    reg->task = smw_create_task(reg, city_registry_task_work);
    if (!reg->task) {
        free(reg);
        return -1;
    }

    return 0;
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
