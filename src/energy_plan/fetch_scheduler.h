/**
 * @file fetch_scheduler.h
 * @brief Periodic fetch scheduler for weather and elpris data.
 *
 * Runs weather and elpris fetches on configurable schedules, saves results
 * through energy_plan_store, then forks the compute binary.
 *
 * Schedule defaults (set via FetchSchedulerConfig):
 *   Weather  – every hour at HH:MM:SS UTC (aligned + offset)
 *   Elpris   – once daily at a given UTC hour:minute
 */

#ifndef FETCH_SCHEDULER_H
#define FETCH_SCHEDULER_H

#include <pthread.h>
#include <signal.h>
#include <stdint.h>

typedef struct {
    /** Set to non-zero to request shutdown. */
    volatile sig_atomic_t* shutdown_flag;

    /**
     * Absolute path to the compute binary to fork after each fetch.
     * NULL disables the compute step (useful for testing).
     */
    const char* compute_exe;

    /** Host of the weather/elpris service. e.g. "127.0.0.1" */
    const char* service_host;

    /** Port of the weather/elpris service. e.g. "10680" */
    const char* service_port;

    /**
     * URL path for the minutely weather forecast endpoint.
     * e.g. "/v1/forecast/minutely"
     * Query string "?lat=…&lon=…" is appended automatically.
     */
    const char* weather_url_path;

    /**
     * URL path for the elpris endpoint. e.g. "/v1/elpris"
     * Query string "?price=SE1" etc. is appended automatically.
     */
    const char* elpris_url_path;

    /**
     * Price zone strings to fetch, e.g. {"SE1","SE2","SE3","SE4"}.
     * The array is read but not owned; must remain valid for the
     * lifetime of the scheduler.
     */
    const char* const* price_zones;
    int                price_zones_count;

    /** Per-request HTTP timeout in milliseconds. */
    unsigned long timeout_ms;

    /** Weather fetch interval in milliseconds. e.g. 60*60*1000 for hourly. */
    uint64_t weather_interval_ms;

    /**
     * Weather fetch alignment offset in milliseconds.
     * e.g. 2*60*1000 to fire at HH:02:00 UTC.
     */
    uint64_t weather_offset_ms;

    /** UTC hour at which to run the daily elpris fetch. */
    int elpris_hour_utc;

    /** UTC minute at which to run the daily elpris fetch. */
    int elpris_minute_utc;
} FetchSchedulerConfig;

/**
 * @brief Start the fetch scheduler in a background thread.
 *
 * Performs an immediate fetch + compute cycle before the first timer fires
 * so the system has data from the moment it starts.
 *
 * @param thread  Output: created thread handle.
 * @param config  Scheduler configuration. Must not be NULL.
 * @return 0 on success, -1 on failure.
 */
int fetch_scheduler_start(pthread_t*                  thread,
                          const FetchSchedulerConfig* config);

/**
 * @brief Stop the scheduler thread and wait for it to exit.
 *
 * @return 0 on success, non-zero on pthread_join failure.
 */
int fetch_scheduler_stop(pthread_t thread);

#endif // FETCH_SCHEDULER_H
