/**
 * @file city_registry.h
 * @brief Async non-blocking city registry backed by a CSV file.
 *
 * Provides a single public entry point, city_registry_initiate(), which
 * launches an SMW task that asynchronously opens, locks, reads, updates, and
 * rewrites the registry CSV without ever blocking the calling thread.
 *
 * A @ref CityRegistryOnDone callback is fired exactly once when the operation
 * completes or fails, after which all internal resources are freed
 * automatically.
 *
 * Typical usage:
 * @code
 *   city_registry_initiate(CITY_REGISTRY_FILE, "Stockholm", "SE3",
 *                          59.334591, 18.063240,
 *                          request_ctx, on_registry_done);
 * @endcode
 */

#ifndef CITY_REGISTRY_H
#define CITY_REGISTRY_H

#include <stdint.h>
#include <time.h>

// ---- Constants --------------------------------------------------------------

/** Maximum number of cities that can be stored in the registry at once. */
#define MAX_REGISTERED_CITIES 200

/** Default path to the registry CSV file. */
#define CITY_REGISTRY_FILE "energy_plan/cities.csv"

/**
 * @brief Time-to-live for a registry entry in seconds (2 days).
 *
 * Entries not accessed within this window are evicted on the next write pass.
 */
#define CITY_TTL_SECONDS (2ULL * 24 * 3600)

// ---- Result type ------------------------------------------------------------

/**
 * @brief Outcome of a city_registry_initiate() operation.
 */
typedef enum {
    CITY_ADDED,  /**< City was not present and has been inserted. */
    CITY_EXISTS, /**< City was already present; its timestamp was updated. */
    CITY_LIMIT_REACHED, /**< Registry is full or an I/O error occurred; city was
                           not added. */
} CityRegisterStatus;

// ---- Completion callback ----------------------------------------------------

/**
 * @brief Callback fired when an async registry operation finishes.
 *
 * Called exactly once per city_registry_initiate() call, whether the
 * operation succeeded or failed. The registry task and all its resources are
 * freed before this callback returns, so the callback must not reference any
 * @ref CityRegistry internals.
 *
 * @param context  Opaque pointer forwarded unchanged from
 * city_registry_initiate().
 * @param status   Outcome of the operation.
 */
typedef void (*CityRegistryOnDone)(void* context, CityRegisterStatus status);

// ---- Entry type -------------------------------------------------------------

/**
 * @brief A single row in the registry CSV.
 *
 * Used internally during parsing and rewriting. Also exposed so that callers
 * with direct access to registry state can read parsed rows if needed.
 */
typedef struct {
    char   city[256];     /**< Null-terminated city name. */
    char   price[16];     /**< Price zone identifier (e.g. "SE3"). */
    double lat;           /**< Latitude in decimal degrees. */
    double lon;           /**< Longitude in decimal degrees. */
    time_t last_accessed; /**< Unix timestamp of the last access; used for TTL
                             eviction. */
} CityEntry;

// ---- State machine states ---------------------------------------------------

/**
 * @brief Internal states of a @ref CityRegistry task.
 *
 * States are advanced by city_registry_task_work() on each SMW tick.
 * Transitions always move forward; there is no backwards branching.
 *
 * Normal path:
 *   INIT -> OPEN -> LOCK -> READ -> PROCESS -> SEEK -> WRITE -> DONE -> DISPOSE
 *
 * Error path from any state:
 *   * -> ERROR -> DISPOSE
 *
 * Short-circuit (registry full, no write needed):
 *   PROCESS -> DONE -> DISPOSE
 */
typedef enum {
    CITY_REGISTRY_STATE_INIT, /**< Initial state; transitions to OPEN on the
                                 first tick. */
    CITY_REGISTRY_STATE_OPEN, /**< Opens or creates the CSV file with
                                 O_NONBLOCK. */
    CITY_REGISTRY_STATE_LOCK, /**< Acquires an exclusive flock; retries on
                                 EWOULDBLOCK. */
    CITY_REGISTRY_STATE_READ, /**< Accumulates file content one chunk per tick
                                 until EOF. */
    CITY_REGISTRY_STATE_PROCESS, /**< Parses the buffer and computes the updated
                                    city table. */
    CITY_REGISTRY_STATE_SEEK,    /**< Rewinds and truncates the file ready for
                                    rewriting. */
    CITY_REGISTRY_STATE_WRITE, /**< Writes the updated table one chunk per tick.
                                */
    CITY_REGISTRY_STATE_DONE,  /**< Operation complete; fires on_done then
                                  transitions to DISPOSE. */
    CITY_REGISTRY_STATE_ERROR, /**< Unrecoverable error; fires on_done with
                                  CITY_LIMIT_REACHED. */
    CITY_REGISTRY_STATE_DISPOSE, /**< Releases all resources and frees the
                                    struct. */
} CityRegistryState;

// ---- Context struct ---------------------------------------------------------

/**
 * @brief Internal context for a single async city-registry operation.
 *
 * Heap-allocated by city_registry_initiate() and freed automatically once the
 * state machine reaches @ref CITY_REGISTRY_STATE_DISPOSE. Callers should treat
 * this struct as opaque; all interaction is through the public API and the
 * @ref CityRegistryOnDone callback.
 */
typedef struct {
    void* task; /**< Opaque SMW task handle; owned by this struct. */

    int fd; /**< File descriptor for the open CSV file, or -1 if not yet open.
             */
    char filepath[512]; /**< Resolved path to the CSV file. */

    char* read_buf; /**< Growable buffer accumulating raw file bytes during
                       READ. */
    size_t
        read_buf_size;   /**< Number of valid bytes currently in @p read_buf. */
    size_t read_buf_cap; /**< Allocated capacity of @p read_buf in bytes. */

    char* write_buf; /**< Serialised CSV content to be written back to the file.
                      */
    size_t write_buf_size; /**< Total size of @p write_buf in bytes. */
    size_t write_offset;   /**< Bytes already flushed; used to resume after
                              EAGAIN. */

    CityEntry cities[MAX_REGISTERED_CITIES]; /**< City table parsed from CSV and
                                                updated in PROCESS. */
    int city_count; /**< Number of entries in @p cities. */

    char   city[256]; /**< Input: city name to register. */
    char   price[16]; /**< Input: price zone identifier. */
    double lat;       /**< Input: latitude in decimal degrees. */
    double lon;       /**< Input: longitude in decimal degrees. */

    CityRegisterStatus result; /**< Outcome determined in PROCESS; reported via
                                  on_done in DONE/ERROR. */

    CityRegistryState state; /**< Current state machine state. */

    void* callback_context; /**< Opaque pointer forwarded to @p on_done. */
    CityRegistryOnDone
        on_done; /**< Completion callback; never NULL after initiation. */
} CityRegistry;

// ---- Public API -------------------------------------------------------------

/**
 * @brief Allocate and start an async city-registry operation.
 *
 * Creates a heap-allocated @ref CityRegistry, registers an SMW task, and
 * returns immediately. The task progresses through its state machine on
 * subsequent SMW ticks without blocking the caller.
 *
 * @p on_done is guaranteed to be called exactly once. On success it receives
 * the true @ref CityRegisterStatus; on any failure it receives
 * @ref CITY_LIMIT_REACHED. After @p on_done returns the @ref CityRegistry
 * has been freed and must not be accessed.
 *
 * @param filepath  Path to the CSV registry file. Pass @ref CITY_REGISTRY_FILE
 *                  for the default location. Must not be NULL.
 * @param city      Null-terminated city name to register. Must not be NULL.
 * @param price     Null-terminated price zone string (e.g. "SE3"). Must not be
 * NULL.
 * @param lat       Latitude of the city in decimal degrees.
 * @param lon       Longitude of the city in decimal degrees.
 * @param context   Opaque pointer forwarded unchanged to @p on_done.
 * @param on_done   Completion callback. Must not be NULL.
 *
 * @return  0  Task created successfully and is now running.
 * @return -1  Invalid arguments or allocation failure; @p on_done is not
 * called.
 */
int city_registry_initiate(const char* filepath, const char* city,
                           const char* price, double lat, double lon,
                           void* context, CityRegistryOnDone on_done);

/**
 * @brief SMW task work function for a city-registry operation.
 *
 * Called by the SMW scheduler on each tick. Advances the @ref CityRegistryState
 * machine by one step, handling EAGAIN / EWOULDBLOCK by returning early and
 * retrying on the next tick.
 *
 * @note This function is registered internally by city_registry_initiate() via
 *       smw_create_task() and must not be called directly.
 *
 * @param context   Pointer to the owning @ref CityRegistry instance.
 * @param mon_time  Monotonic timestamp provided by the SMW scheduler (currently
 * unused).
 */
void city_registry_task_work(void* context, uint64_t mon_time);

/**
 * @brief Release all resources held by a city-registry context.
 *
 * Unlocks and closes the file descriptor if open, destroys the SMW task,
 * frees all heap buffers, and frees the @ref CityRegistry struct itself.
 *
 * This is called automatically when the state machine reaches
 * @ref CITY_REGISTRY_STATE_DISPOSE. Callers should not need to invoke it
 * directly unless aborting an operation externally.
 *
 * @param reg  Registry context to dispose. If NULL this function is a no-op.
 */
void city_registry_dispose(CityRegistry* reg);

#endif // !CITY_REGISTRY_H
