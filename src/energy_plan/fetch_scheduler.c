#include "fetch_scheduler.h"

#include "fetcher.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_MODULE "FETCH_SCHEDULER"

#define FETCH_PORT "10680"
#define FETCH_TIMEOUT_MS 10000

/* Weather: fetch every hour at minute >= 02 UTC */
#define WEATHER_SCHEDULE_MIN 2
#define WEATHER_CACHE_TTL_S (62 * 60)

/* Elpris: fetch once daily at 13:05 UTC */
#define ELPRIS_SCHEDULE_HOUR 13
#define ELPRIS_SCHEDULE_MIN 5
#define ELPRIS_CACHE_TTL_S (65 * 60 * 24)

/* ============= Internal types ============= */

typedef enum { FETCH_TYPE_WEATHER, FETCH_TYPE_ELPRIS } FetchType;

typedef struct {
    ComputeConfig cfg;
    FetchType     type;
} FetchComputeArg;

struct FetchScheduler {
    ThreadPool*   pool;
    ComputeConfig cfg;
    long last_weather_fetch_h; /* epoch-hours of last fetch, -1=never */
    long last_elpris_fetch_d;  /* epoch-days of last fetch, -1=never */
};

/* ============= Fetch helpers (blocking — run in worker thread) =============
 */

static void do_fetch_weather(const ComputeConfig* cfg) {
    LOG_INFO(LOG_MODULE, "Fetching weather forecasts for compute...");

    FileCacheConfig cache_cfg = {
        .cache_dir   = cfg->compute_input_dir,
        .ttl_seconds = WEATHER_CACHE_TTL_S,
        .enabled     = true,
    };

    FileCacheInstance* cache = file_cache_create(&cache_cfg);
    if (!cache) {
        LOG_ERROR(LOG_MODULE, "Failed to create cache for weather");
        return;
    }

    fetch_all_city_forecasts(cache, FETCH_PORT, FETCH_TIMEOUT_MS);
    file_cache_destroy(cache);
}

static void do_fetch_elpris(const ComputeConfig* cfg) {
    LOG_INFO(LOG_MODULE, "Fetching elpris for compute...");

    FileCacheConfig cache_cfg = {
        .cache_dir   = cfg->compute_input_dir,
        .ttl_seconds = ELPRIS_CACHE_TTL_S,
        .enabled     = true,
    };

    FileCacheInstance* cache = file_cache_create(&cache_cfg);
    if (!cache) {
        LOG_ERROR(LOG_MODULE, "Failed to create cache for elpris");
        return;
    }

    fetch_all_price_groups(cache, "elpris", FETCH_PORT, FETCH_TIMEOUT_MS);
    file_cache_destroy(cache);
}

/* ============= Thread pool work function ============= */

static int fetch_and_compute_work(void* arg, ThreadPoolTask* task) {
    (void)task;
    FetchComputeArg* fca = arg;

    if (fca->type == FETCH_TYPE_WEATHER) {
        do_fetch_weather(&fca->cfg);
    } else {
        do_fetch_elpris(&fca->cfg);
    }

    int result = compute_run(&fca->cfg);
    free(fca);
    return result;
}

static void fetch_and_compute_done(void* arg, int status) {
    (void)arg;
    if (status != 0) {
        LOG_WARN(LOG_MODULE, "fetch+compute task finished with status %d",
                 status);
    } else {
        LOG_INFO(LOG_MODULE, "fetch+compute task completed successfully");
    }
}

/* ============= Submit helper ============= */

static void submit_fetch(FetchScheduler* sched, FetchType type) {
    if (!sched->pool) {
        return;
    }

    FetchComputeArg* arg = malloc(sizeof(*arg));
    if (!arg) {
        LOG_ERROR(LOG_MODULE, "OOM: cannot allocate fetch+compute arg");
        return;
    }
    arg->cfg  = sched->cfg;
    arg->type = type;

    if (!thread_pool_submit(sched->pool, fetch_and_compute_work, arg,
                            fetch_and_compute_done, NULL, 0, -1)) {
        LOG_ERROR(LOG_MODULE, "Failed to submit fetch+compute task");
        free(arg);
    }
}

/* ============= Public API ============= */

FetchScheduler* fetch_scheduler_create(ThreadPool*          pool,
                                       const ComputeConfig* cfg) {
    FetchScheduler* sched = malloc(sizeof(*sched));
    if (!sched) {
        return NULL;
    }

    sched->pool                 = pool;
    sched->cfg                  = *cfg;
    sched->last_weather_fetch_h = -1;
    sched->last_elpris_fetch_d  = -1;

    /* Submit initial fetch+compute on startup */
    submit_fetch(sched, FETCH_TYPE_WEATHER);
    submit_fetch(sched, FETCH_TYPE_ELPRIS);

    /* Mark current slot as fetched so the SMW tick does not re-submit */
    time_t now                  = time(NULL);
    sched->last_weather_fetch_h = (long)(now / 3600);
    sched->last_elpris_fetch_d  = (long)(now / 86400);

    LOG_INFO(LOG_MODULE, "Fetch scheduler created (initial fetch submitted)");
    return sched;
}

void fetch_scheduler_destroy(FetchScheduler* sched) { free(sched); }

void fetch_scheduler_smw_callback(void* context, uint64_t mon_time) {
    (void)mon_time;
    FetchScheduler* sched = context;

    time_t    now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);

    long epoch_h = (long)(now / 3600);
    long epoch_d = (long)(now / 86400);

    /* Weather: every hour at minute >= WEATHER_SCHEDULE_MIN */
    if (utc.tm_min >= WEATHER_SCHEDULE_MIN &&
        epoch_h != sched->last_weather_fetch_h) {
        LOG_INFO(LOG_MODULE, "Scheduling hourly weather fetch (UTC %02d:%02d)",
                 utc.tm_hour, utc.tm_min);
        sched->last_weather_fetch_h = epoch_h;
        submit_fetch(sched, FETCH_TYPE_WEATHER);
    }

    /* Elpris: once daily at hour >= 13, minute >= 05 */
    if (utc.tm_hour >= ELPRIS_SCHEDULE_HOUR &&
        utc.tm_min >= ELPRIS_SCHEDULE_MIN &&
        epoch_d != sched->last_elpris_fetch_d) {
        LOG_INFO(LOG_MODULE, "Scheduling daily elpris fetch (UTC %02d:%02d)",
                 utc.tm_hour, utc.tm_min);
        sched->last_elpris_fetch_d = epoch_d;
        submit_fetch(sched, FETCH_TYPE_ELPRIS);
    }
}
