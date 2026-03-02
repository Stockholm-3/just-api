/**
 * @file energy_plan_store.h
 * @brief Central store for the energy-plan pipeline.
 *
 * Owns every path, lock, and I/O operation under the energy_plan/ directory:
 *
 *   energy_plan/
 *   ├── cities.csv                         – city registry
 *   ├── compute_input/
 *   │   ├── <city_lower>-<lat>-<lon>.json  – per-city weather blobs
 *   │   └── elpris_merged.json             – merged SE1-SE4 price data
 *   └── compute_output/
 *       ├── .lock                          – flock target (EX=compute, SH=read)
 *       └── <City>-<Zone>.json             – per-city compute results
 *
 * No other translation unit builds paths into energy_plan/, performs I/O
 * there, or touches the output lock directly.
 *
 * Blocking vs non-blocking
 * ------------------------
 *  The following block the calling thread – call from worker/scheduler
 *  threads, never from an SMW task:
 *    energy_plan_store_load_cities()
 *    energy_plan_store_save_weather()
 *    energy_plan_store_save_elpris()
 *    energy_plan_store_acquire_write_lock()
 *    energy_plan_store_release_write_lock()
 *    energy_plan_store_clear_outputs()
 *    energy_plan_store_write_output()
 *    energy_plan_store_get_weather_path()   – pure path build, no I/O
 *    energy_plan_store_get_elpris_path()    – pure path build, no I/O
 *    energy_plan_store_get_output_path()    – pure path build, no I/O
 *
 *  The following are non-blocking SMW tasks:
 *    energy_plan_store_register_city()
 *    energy_plan_store_read_output_async()
 */

#ifndef ENERGY_PLAN_STORE_H
#define ENERGY_PLAN_STORE_H

#include <jansson.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    /**
     * Root directory for all energy-plan data.
     * Sub-paths are constructed relative to this.
     * Example: "energy_plan"
     */
    const char* base_dir;

    /** Maximum number of cities tracked in the registry. */
    int max_cities;

    /** Seconds before an unaccessed city is expired. */
    unsigned long city_ttl_seconds;
} EpStoreConfig;

/**
 * @brief Initialise the store.
 *
 * Creates required sub-directories. Must be called once before any other
 * energy_plan_store_* function. Not thread-safe with itself – call from main.
 *
 * @return 0 on success, -1 on failure.
 */
int energy_plan_store_init(const EpStoreConfig* config);

/** Release resources held by the store. Call on shutdown. */
void energy_plan_store_shutdown(void);

typedef struct {
    char   city[256];
    char   price[16];
    double lat;
    double lon;
    time_t last_accessed;
} EpCityEntry;

typedef struct {
    EpCityEntry* entries;
    int          count;
} EpCityList;

typedef enum {
    EP_CITY_ADDED,
    EP_CITY_EXISTS,
    EP_CITY_LIMIT_REACHED,
} EpCityRegisterStatus;

typedef void (*EpCityOnDone)(void* context, EpCityRegisterStatus status);

/**
 * @brief Async: register or refresh a city in the CSV registry.
 *
 * Non-blocking SMW task. Fires @p on_done exactly once.
 * On internal error the callback receives EP_CITY_LIMIT_REACHED.
 *
 * @return 0 on successful task creation, -1 on error.
 */
int energy_plan_store_register_city(const char* city, const char* price,
                                    double lat, double lon, void* context,
                                    EpCityOnDone on_done);

/**
 * @brief Sync: load all non-expired cities from the registry.
 *
 * Caller must free(result.entries). Returns {NULL, 0} on error.
 */
EpCityList energy_plan_store_load_cities(void);

/**
 * @brief Save a weather forecast blob for one city.
 *
 * Always deletes any existing file before writing.
 *
 * @return 0 on success, -1 on failure.
 */
int energy_plan_store_save_weather(const char* city, double lat, double lon,
                                   json_t* data);

/**
 * @brief Save the merged elpris JSON array.
 *
 * Always deletes the existing file before writing.
 *
 * @return 0 on success, -1 on failure.
 */
int energy_plan_store_save_elpris(json_t* merged_array);

/**
 * @brief Build the filesystem path for a city's weather input file.
 *
 * Pure path construction – does not touch the filesystem.
 *
 * @return 0 on success, -1 if @p out is too small.
 */
int energy_plan_store_get_weather_path(const char* city, double lat, double lon,
                                       char* out, size_t out_size);

/**
 * @brief Build the filesystem path for the merged elpris input file.
 *
 * Pure path construction – does not touch the filesystem.
 *
 * @return 0 on success, -1 if @p out is too small.
 */
int energy_plan_store_get_elpris_path(char* out, size_t out_size);

/**
 * @brief Acquire an exclusive write lock on the output directory.
 *
 * Blocks until all shared readers have released.
 * Returns an opaque handle for energy_plan_store_release_write_lock().
 *
 * @return fd >= 0 on success, -1 on failure.
 */
int energy_plan_store_acquire_write_lock(void);

/**
 * @brief Release the exclusive write lock.
 *
 * @param lock_handle Value returned by acquire_write_lock().
 */
void energy_plan_store_release_write_lock(int lock_handle);

/**
 * @brief Delete every *.json file in the output directory.
 *
 * Must be called while holding the exclusive write lock.
 *
 * @return 0 on success, -1 if any unlink failed.
 */
int energy_plan_store_clear_outputs(void);

/**
 * @brief Build the filesystem path for a city's compute output file.
 *
 * Pure path construction – does not touch the filesystem.
 * Pattern: <output_dir>/<city>-<zone>.json
 *
 * @return 0 on success, -1 if @p out is too small.
 */
int energy_plan_store_get_output_path(const char* city, const char* zone,
                                      char* out, size_t out_size);

/**
 * @brief Write a compute result JSON object for a city atomically.
 *
 * Writes to a temp file then renames into place.
 * Must be called while holding the exclusive write lock.
 *
 * @return 0 on success, -1 on failure.
 */
int energy_plan_store_write_output(const char* city, const char* zone,
                                   json_t* data);

typedef enum {
    EP_OUTPUT_OK,         // File read; buf/len are valid.
    EP_OUTPUT_NOT_FOUND,  // File does not exist yet.
    EP_OUTPUT_LOCK_ERROR, // Could not acquire shared read lock.
    EP_OUTPUT_READ_ERROR, // File exists but could not be read.
} EpOutputStatus;

/**
 * Callback fired when energy_plan_store_read_output_async() completes.
 *
 * On EP_OUTPUT_OK, @p buf is a heap-allocated NUL-terminated JSON string.
 * The callback takes ownership and must free() it.
 * On all other statuses @p buf is NULL and @p len is 0.
 */
typedef void (*EpOutputOnDone)(void* context, EpOutputStatus status, char* buf,
                               size_t len);

/**
 * @brief Async: read the compute-output JSON for a city + price zone.
 *
 * Acquires LOCK_SH on the output lock file (non-blocking, retries each
 * SMW tick), reads the file, fires @p on_done. Callback owns the buffer.
 *
 * @return 0 on successful task creation, -1 on error.
 */
int energy_plan_store_read_output_async(const char* city, const char* price,
                                        void* context, EpOutputOnDone on_done);

/**
 * Algorithm callback invoked once per city by energy_plan_store_run_compute().
 *
 * The store loads all data and passes it in as jansson arrays so the
 * algorithm has no filesystem dependencies. Fill @p out_decisions
 * (length @p slots_per_day) with true = buy, false = sell per slot.
 *
 * @param city          City name.
 * @param zone_str      Price zone e.g. "SE3".
 * @param weather       JSON array of weather objects, each with keys:
 *                      time, temperature, humidity, precipitation,
 *                      weather_code, windspeed, wind_direction,
 *                      pressure, is_day.
 * @param weather_count Number of valid entries in @p weather
 *                      (may be < @p slots_per_day if data is partial).
 * @param prices        JSON array of price objects, each with keys:
 *                      SEK_per_kWh, time_start, time_end.
 *                      Always length == @p slots_per_day.
 * @param slots_per_day Total time slots per day.
 * @param out_decisions Caller-allocated bool[slots_per_day] to fill.
 */
typedef void (*EpAlgorithmFn)(const char* city, const char* zone_str,
                              json_t* weather, int weather_count,
                              json_t* prices, int slots_per_day,
                              bool* out_decisions);

/**
 * @brief Blocking: run the full compute pipeline for all registered cities.
 *
 * Acquires the exclusive write lock, loads cities + elpris, iterates every
 * city, loads its weather input, calls @p algorithm_fn to get decisions,
 * clears old outputs, writes new result files, then releases the lock.
 *
 * All I/O is handled internally — the compute binary only needs to supply
 * the algorithm function and call init/shutdown.
 *
 * @param slots_per_day  15-min slots per day, typically 96.
 * @param algorithm_fn   Decision function. Must not be NULL.
 * @return  0  All cities processed successfully.
 * @return  1  One or more cities failed (partial success).
 * @return -1  Fatal error (lock, no cities, no elpris).
 */
int energy_plan_store_run_compute(int           slots_per_day,
                                  EpAlgorithmFn algorithm_fn);

void ep_registry_task_work(void* context, uint64_t mon_time);
void ep_output_reader_task_work(void* context, uint64_t mon_time);

#endif // ENERGY_PLAN_STORE_H
