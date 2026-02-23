/**
 * @file city_registry.h
 * @brief Async non-blocking city registry backed by a CSV file.
 *
 * Provides two public async entry points:
 *
 *  - city_registry_initiate()      – register/update a city in the CSV.
 *  - city_output_read_initiate()   – read a city's compute-output JSON
 *                                    under a shared flock, then fire a
 *                                    callback with the file contents.
 *
 * Both operations run as SMW tasks and never block the calling thread.
 */

#ifndef CITY_REGISTRY_H
#define CITY_REGISTRY_H

#include <stdint.h>
#include <time.h>

/* ── Constants ──────────────────────────────────────────────────────────────
 */

#define MAX_REGISTERED_CITIES 200
#define CITY_REGISTRY_FILE "energy_plan/cities.csv"
#define CITY_TTL_SECONDS (2ULL * 24 * 3600)

/** Directory where energy_parser writes per-city JSON files. */
#define COMPUTE_OUTPUT_DIR "energy_plan/compute_output"

/** Lock file shared between energy_parser and all readers. */
#define COMPUTE_OUTPUT_LOCK "energy_plan/compute_output/.lock"

/* ── Registry result type ───────────────────────────────────────────────────
 */

typedef enum {
    CITY_ADDED,
    CITY_EXISTS,
    CITY_LIMIT_REACHED,
} CityRegisterStatus;

typedef void (*CityRegistryOnDone)(void* context, CityRegisterStatus status);

/* ── Compute-output reader result ───────────────────────────────────────────
 */

typedef enum {
    CITY_OUTPUT_OK,         /**< File read successfully; buf/len are valid. */
    CITY_OUTPUT_NOT_FOUND,  /**< File does not exist yet (parser not run).  */
    CITY_OUTPUT_LOCK_ERROR, /**< Could not acquire shared lock.             */
    CITY_OUTPUT_READ_ERROR, /**< File exists but could not be read.         */
} CityOutputStatus;

/**
 * @brief Callback fired when city_output_read_initiate() completes.
 *
 * @param context  Opaque pointer forwarded from city_output_read_initiate().
 * @param status   Outcome of the read.
 * @param buf      Heap-allocated, NUL-terminated JSON string on
 *                 CITY_OUTPUT_OK, NULL otherwise.  The callback takes
 *                 ownership and must free() it.
 * @param len      Length of @p buf in bytes (excluding the NUL terminator),
 *                 or 0 on failure.
 */
typedef void (*CityOutputOnDone)(void* context, CityOutputStatus status,
                                 char* buf, size_t len);

/* ── Shared entry / load types ──────────────────────────────────────────────
 */

typedef struct {
    char   city[256];
    char   price[16];
    double lat;
    double lon;
    time_t last_accessed;
} CityEntry;

typedef struct {
    CityEntry* entries;
    int        count;
} CityLoadResult;

/* ── Registry state machine ─────────────────────────────────────────────────
 */

typedef enum {
    CITY_REGISTRY_STATE_INIT,
    CITY_REGISTRY_STATE_OPEN,
    CITY_REGISTRY_STATE_LOCK,
    CITY_REGISTRY_STATE_READ,
    CITY_REGISTRY_STATE_PROCESS,
    CITY_REGISTRY_STATE_SEEK,
    CITY_REGISTRY_STATE_WRITE,
    CITY_REGISTRY_STATE_DONE,
    CITY_REGISTRY_STATE_ERROR,
    CITY_REGISTRY_STATE_DISPOSE,
} CityRegistryState;

typedef struct {
    void* task;

    int  fd;
    char filepath[512];

    char*  read_buf;
    size_t read_buf_size;
    size_t read_buf_cap;

    char*  write_buf;
    size_t write_buf_size;
    size_t write_offset;

    CityEntry cities[MAX_REGISTERED_CITIES];
    int       city_count;

    char   city[256];
    char   price[16];
    double lat;
    double lon;

    CityRegisterStatus result;
    CityRegistryState  state;

    void*              callback_context;
    CityRegistryOnDone on_done;
} CityRegistry;

/* ── Output-reader state machine ────────────────────────────────────────────
 */

typedef enum {
    CITY_OUTPUT_STATE_INIT,
    CITY_OUTPUT_STATE_LOCK,  /**< Open lock file, acquire LOCK_SH | LOCK_NB. */
    CITY_OUTPUT_STATE_OPEN,  /**< Open the JSON output file.                 */
    CITY_OUTPUT_STATE_READ,  /**< Read file contents one chunk per tick.     */
    CITY_OUTPUT_STATE_DONE,  /**< Fire callback with buffer.                 */
    CITY_OUTPUT_STATE_ERROR, /**< Fire callback with error status.           */
    CITY_OUTPUT_STATE_DISPOSE,
} CityOutputState;

typedef struct {
    void* task;

    int lock_fd; /**< fd for the .lock file (shared flock held).  */
    int file_fd; /**< fd for the JSON output file.                */

    char city[256];      /**< City name (used to build the file path).    */
    char file_path[512]; /**< Full path to the JSON file.                 */

    char*  read_buf;
    size_t read_buf_size;
    size_t read_buf_cap;

    CityOutputStatus result;
    CityOutputState  state;

    void*            callback_context;
    CityOutputOnDone on_done;
} CityOutputReader;

/* ── Public API ─────────────────────────────────────────────────────────────
 */

/**
 * @brief Synchronously load all non-expired cities from the registry CSV.
 * Blocks the calling thread. Do not call from an SMW task.
 */
CityLoadResult city_registry_load_all(const char* filepath);

/**
 * @brief Start an async city-register-or-update operation.
 * Fires @p on_done exactly once.
 */
int city_registry_initiate(const char* filepath, const char* city,
                           const char* price, double lat, double lon,
                           void* context, CityRegistryOnDone on_done);

/** @internal SMW task work function – do not call directly. */
void city_registry_task_work(void* context, uint64_t mon_time);

/** Release all resources held by a registry context. */
void city_registry_dispose(CityRegistry* reg);

/**
 * @brief Start an async read of a city's compute-output JSON.
 *
 * Acquires a shared flock on COMPUTE_OUTPUT_LOCK (non-blocking, retries
 * each tick), reads the file for @p city, then fires @p on_done with the
 * heap-allocated content.  The callback owns the buffer and must free() it.
 *
 * @param city      City name — must match the filename written by the parser.
 * @param context   Forwarded unchanged to @p on_done.
 * @param on_done   Completion callback. Must not be NULL.
 * @return 0 on successful task creation, -1 on argument / allocation error.
 */
int city_output_read_initiate(const char* city, void* context,
                              CityOutputOnDone on_done);

/** @internal SMW task work function – do not call directly. */
void city_output_reader_task_work(void* context, uint64_t mon_time);

/** Release all resources held by an output-reader context. */
void city_output_reader_dispose(CityOutputReader* reader);

#endif /* CITY_REGISTRY_H */
