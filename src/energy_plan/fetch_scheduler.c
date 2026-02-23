#include "fetch_scheduler.h"

#include "fetcher.h"
#include "logger.h"
#include "sys/wait.h"

#include <scheduler.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#define FETCH_PORT "10680"
#define FETCH_TIMEOUT_MS 10000

// Weather: fetch every hour at 00:02 UTC
#define WEATHER_INTERVAL_MS (60ULL * 60 * 1000)
#define WEATHER_OFFSET_MS (60ULL * 2 * 1000)
#define WEATHER_CACHE_TTL_S (62 * 60) // 2 min margin over the interval

// Elpris: fetch once daily at 13:05 UTC
#define ELPRIS_SCHEDULE_HOUR 13
#define ELPRIS_SCHEDULE_MIN 5
#define ELPRIS_CACHE_TTL_S (65 * 60 * 24) // 5 min margin over the interval

static void fork_compute(const char* exe) {
    if (!exe) {
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("FETCH_SCHEDULER", "fork() failed for compute step");
        return;
    }

    if (pid == 0) {
        execl(exe, exe, (char*)NULL);
        LOG_ERROR("FETCH_SCHEDULER", "execl() failed for: %s", exe);
        _exit(1);
    }

    // IGNORE THE CHILD!!!! :):)
    int   status;
    pid_t wpid;
    do {
        wpid = waitpid(pid, &status, WNOHANG);
    } while (wpid > 0);
}

typedef struct {
    volatile sig_atomic_t* shutdown;
    const char*            compute_exe;
} SchedulerContext;

static void fetch_weather(void) {
    LOG_INFO("FETCH_SCHEDULER", "Fetching weather forecasts for compute...");

    FileCacheConfig cfg = {
        .cache_dir   = "./cache/compute_input",
        .ttl_seconds = WEATHER_CACHE_TTL_S,
        .enabled     = true,
    };

    FileCacheInstance* cache = file_cache_create(&cfg);
    if (!cache) {
        LOG_ERROR("FETCH_SCHEDULER", "Failed to create cache for weather");
        return;
    }

    fetch_all_city_forecasts(cache, FETCH_PORT, FETCH_TIMEOUT_MS);

    file_cache_destroy(cache);
}

static void fetch_elpris(void) {
    LOG_INFO("FETCH_SCHEDULER", "Fetching elpris for compute...");

    FileCacheConfig cfg = {
        .cache_dir   = "./cache/compute_input",
        .ttl_seconds = ELPRIS_CACHE_TTL_S,
        .enabled     = true,
    };

    FileCacheInstance* cache = file_cache_create(&cfg);
    if (!cache) {
        LOG_ERROR("FETCH_SCHEDULER", "Failed to create cache for elpris");
        return;
    }

    fetch_all_price_groups(cache, "elpris", FETCH_PORT, FETCH_TIMEOUT_MS);

    file_cache_destroy(cache);
}

static const char* g_compute_exe = NULL;

static void scheduled_weather(void) {
    fetch_weather();
    fork_compute(g_compute_exe);
}

static void scheduled_elpris(void) {
    fetch_elpris();
    fork_compute(g_compute_exe);
}

static void* scheduler_thread(void* arg) {
    SchedulerContext* ctx = arg;

    g_compute_exe = ctx->compute_exe;

    SchedulerTimer* hourly = create_aligned_timer_utc(
        WEATHER_INTERVAL_MS, WEATHER_OFFSET_MS, scheduled_weather);

    SchedulerTimer* daily = create_daily_timer(
        ELPRIS_SCHEDULE_HOUR, ELPRIS_SCHEDULE_MIN, 0, scheduled_elpris);

    SchedulerTimer* timers[] = {hourly, daily};

    run_scheduler(timers, 2, ctx->shutdown);

    destroy_timer(hourly);
    destroy_timer(daily);

    free(ctx);
    return NULL;
}

int fetch_scheduler_start(pthread_t*                    thread,
                          const SchedulerServiceConfig* config) {
    // Fetch on startup so compute has data before the first timer fires
    fetch_weather();
    fetch_elpris();
    fork_compute(config->compute_exe);

    SchedulerContext* ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -1;
    }

    ctx->shutdown    = config->shutdown_flag;
    ctx->compute_exe = config->compute_exe;

    if (pthread_create(thread, NULL, scheduler_thread, ctx) != 0) {
        free(ctx);
        return -1;
    }

    return 0;
}

int fetch_scheduler_stop(pthread_t thread) {
    return pthread_join(thread, NULL);
}
