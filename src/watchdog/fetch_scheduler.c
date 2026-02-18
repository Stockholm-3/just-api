#include "fetch_scheduler.h"

#include "fetcher.h"
#include "logger/logger.h"

#include <scheduler.h>
#include <stddef.h>
#include <stdlib.h>

static void fetch_weather(void) {
    LOG_INFO("FETCH_SCHEDULER", "Fetching Weather for compute...");
}

static void fetch_elpris(void) {

    LOG_INFO("FETCH_SCHEDULER", "Fetching Elpris for compute...");
    FileCacheConfig cfg = {.cache_dir   = "./cache/compute_input",
                           .ttl_seconds = 60 * 60 * 24, // 1 day
                           .enabled     = true};

    FileCacheInstance* cache = file_cache_create(&cfg);

    if (!cache) {
        LOG_ERROR("FETCH_SCHEDULER", "failed to create cache");
        return;
    }

    // Only call fetch if cache was created successfully
    fetch_all_price_groups_sync(cache, "elpris", "10680", 10000);

    file_cache_destroy(cache);
}

typedef struct {
    volatile sig_atomic_t* shutdown;
} SchedulerContext;

static void* scheduler_thread(void* arg) {
    SchedulerContext* ctx = arg;

    SchedulerTimer* quarter_hour =
        create_aligned_timer_utc(15ULL * 60 * 1000, // 15 minutes in ms
                                 60ULL * 1000,      // offset = 00:01 UTC
                                 fetch_weather);

    SchedulerTimer* daily = create_daily_timer(13, 5, 0, fetch_elpris);

    SchedulerTimer* timers[] = {quarter_hour, daily};

    run_scheduler(timers, 2, ctx->shutdown);

    destroy_timer(quarter_hour);
    destroy_timer(daily);

    free(ctx);
    return NULL;
}

int fetch_scheduler_start(pthread_t*                    thread,
                          const SchedulerServiceConfig* config) {
    // Fetch hourly on startup
    fetch_weather();

    SchedulerContext* ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -1;
    }

    ctx->shutdown = config->shutdown_flag;

    if (pthread_create(thread, NULL, scheduler_thread, ctx) != 0) {
        free(ctx);
        return -1;
    }

    return 0;
}

int fetch_scheduler_stop(pthread_t thread) {
    return pthread_join(thread, NULL);
}
