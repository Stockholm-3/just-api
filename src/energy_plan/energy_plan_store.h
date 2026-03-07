/**
 * @file energy_plan_store.h
 * @brief Central I/O coordinator for the energy-plan pipeline.
 *
 * Owns every path and every file under energy_plan/:
 *
 *   energy_plan/
 *   ├── cities.csv
 *   ├── compute_input/
 *   │   ├── <city_lower>-<lat>-<lon>.json
 *   │   └── elpris_merged.json
 *   └── compute_output/
 *       ├── .lock
 *       └── <City>-<Zone>.json
 *
 * General-purpose mechanisms (flock, async file reader, CSV registry)
 * live in lib/ and are used here — not re-implemented.
 *
 * Blocking calls — use from worker threads, never from SMW tasks:
 *   energy_plan_store_load_cities()
 *   energy_plan_store_save_weather()
 *   energy_plan_store_save_elpris()
 *   energy_plan_store_acquire_write_lock()
 *   energy_plan_store_release_write_lock()
 *   energy_plan_store_clear_outputs()
 *   energy_plan_store_write_output()
 *
 * Non-blocking SMW tasks:
 *   energy_plan_store_register_city()
 *   energy_plan_store_read_output_async()
 *
 * Path builders (pure, no I/O):
 *   energy_plan_store_get_weather_path()
 *   energy_plan_store_get_elpris_path()
 *   energy_plan_store_get_output_path()
 */
#ifndef ENERGY_PLAN_STORE_H
#define ENERGY_PLAN_STORE_H

#include "csv_registry.h"

#include <jansson.h>
#include <stddef.h>

typedef struct {
    const char* base_dir;   /**< Root directory, e.g. "energy_plan" */
    int         max_cities; /**< Maximum rows in cities.csv          */
} EpStoreConfig;

/**
 * Initialise the store. Creates sub-directories. Call once from main.
 * @return 0 on success, -1 on failure.
 */
int energy_plan_store_init(const EpStoreConfig* config);

/** Release resources. Call on shutdown. */
void energy_plan_store_shutdown(void);

/**
 * CsvRow fields map to: key=city  tag=price_zone  f1=lat  f2=lon
 */
typedef CsvRow       EpCityEntry;
typedef CsvRegStatus EpCityRegisterStatus;
#define EP_CITY_ADDED CSV_REG_ADDED
#define EP_CITY_EXISTS CSV_REG_EXISTS
#define EP_CITY_LIMIT_REACHED CSV_REG_LIMIT_REACHED

typedef struct {
    EpCityEntry* entries;
    int          count;
} EpCityList;

typedef void (*EpCityOnDone)(void* context, EpCityRegisterStatus status);

/**
 * Async: register or refresh a city. Non-blocking SMW task.
 * @return 0 on success, -1 on error.
 */
int energy_plan_store_register_city(const char* city, const char* price,
                                    double lat, double lon, void* context,
                                    EpCityOnDone on_done);

/**
 * Sync: load all cities from the registry.
 * Caller must free(result.entries). Returns {NULL,0} on error.
 */
EpCityList energy_plan_store_load_cities(void);

int energy_plan_store_save_weather(const char* city, double lat, double lon,
                                   json_t* data);

int energy_plan_store_save_elpris(json_t* merged_array);

int energy_plan_store_get_weather_path(const char* city, double lat, double lon,
                                       char* out, size_t out_size);

int energy_plan_store_get_elpris_path(char* out, size_t out_size);

/** Acquire exclusive write lock. Returns fd handle or -1. Blocks. */
int energy_plan_store_acquire_write_lock(void);

/** Release lock handle returned by acquire. */
void energy_plan_store_release_write_lock(int lock_handle);

int energy_plan_store_get_output_path(const char* city, const char* zone,
                                      char* out, size_t out_size);

/** Delete all *.json files in the output directory. Hold write lock first. */
int energy_plan_store_clear_outputs(void);

/** Write result JSON atomically. Hold write lock first. */
int energy_plan_store_write_output(const char* city, const char* zone,
                                   json_t* data);

typedef enum {
    EP_OUTPUT_OK,
    EP_OUTPUT_NOT_FOUND,
    EP_OUTPUT_LOCK_ERROR,
    EP_OUTPUT_READ_ERROR,
} EpOutputStatus;

/**
 * Callback — on EP_OUTPUT_OK, buf is heap-allocated; caller must free().
 * On all other statuses buf==NULL, len==0.
 */
typedef void (*EpOutputOnDone)(void* context, EpOutputStatus status, char* buf,
                               size_t len);

/**
 * Async: read compute-output JSON for city+zone. Non-blocking SMW task.
 * @return 0 on success, -1 on error.
 */
int energy_plan_store_read_output_async(const char* city, const char* price,
                                        void* context, EpOutputOnDone on_done);

#endif // ENERGY_PLAN_STORE_H
