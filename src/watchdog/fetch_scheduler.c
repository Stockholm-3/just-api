#include "fetch_scheduler.h"

#include "fetcher.h"

#include <scheduler.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static void hourly_fetch(void) { printf("One hour passed\n"); }

static void daily_fetch(void) {
    printf("13:00 reached\n");
    FileCacheConfig cfg = {.cache_dir   = "./compute_input",
                           .ttl_seconds = 60 * 60 * 24, // 1 day
                           .enabled     = true};

    FileCacheInstance* cache = file_cache_create(&cfg);

    if (!cache) {
        printf("Failed to create cache\n");
        return;
    }

    // Only call fetch if cache was created successfully
    fetch_all_price_groups_sync(cache, "elpris", "10680", 10000);

    file_cache_destroy(cache);
    // fetch elpris
}

typedef struct {
    volatile sig_atomic_t* shutdown;
} SchedulerContext;

static void* scheduler_thread(void* arg) {
    SchedulerContext* ctx = arg;

    SchedulerTimer* hourly =
        create_interval_timer((uint64_t)(1000 * 60 * 60), hourly_fetch);

    SchedulerTimer* daily = create_daily_timer(13, 5, 0, daily_fetch);

    SchedulerTimer* timers[] = {hourly, daily};

    run_scheduler(timers, 2, ctx->shutdown);

    destroy_timer(hourly);
    destroy_timer(daily);

    free(ctx);
    return NULL;
}

int fetch_scheduler_start(pthread_t*                    thread,
                          const SchedulerServiceConfig* config) {

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
