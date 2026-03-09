#include "fetch_scheduler.h"

#include "energy_plan/energy_plan_store.h"
#include "logger/logger.h"

#include <http_client.h>
#include <jansson.h>
#include <scheduler.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utils.h>

#define LOG_MOD "FETCH_SCHED"

typedef struct {
    int   done;
    int   success;
    char* body;
} SyncHttp;

static void sync_http_cb(const char* event, const char* response, void* ctx) {
    SyncHttp* h = (SyncHttp*)ctx;

    if (strcmp(event, "RESPONSE") == 0 && response) {
        free(h->body);
        h->body    = strdup(response);
        h->success = h->body != NULL;
        h->done    = 1;
    } else if (strcmp(event, "ERROR") == 0 || strcmp(event, "TIMEOUT") == 0) {
        h->success = 0;
        h->done    = 1;
    }
}

static char* http_get(const char* url, const char* port,
                      unsigned long timeout_ms) {
    SyncHttp    h      = {0};
    HttpClient* client = NULL;

    if (http_client_init(url, &client, port) != 0) {
        LOG_WARN(LOG_MOD, "http_client_init failed: %s", url);
        return NULL;
    }
    client->callback = sync_http_cb;
    client->context  = &h;
    client->timeout  = (uint64_t)timeout_ms;
    client->state    = HTTP_CLIENT_STATE_INIT;

    while (!h.done) {
        http_client_work(client, system_monotonic_ms());
    }

    /* Callback marks completion before the client object is reclaimed. */
    if (client != NULL) {
        http_client_dispose(&client);
    }

    return h.body;
}

static void fork_compute(const char* exe) {
    if (!exe) {
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_WARN(LOG_MOD, "fork() failed");
        return;
    }
    if (pid == 0) {
        execl(exe, exe, (char*)NULL);
        LOG_WARN(LOG_MOD, "execl(%s) failed", exe);
        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        LOG_WARN(LOG_MOD, "compute exited with code %d", WEXITSTATUS(status));
    }
}

static void fetch_weather(const FetchSchedulerConfig* cfg) {
    LOG_INFO(LOG_MOD, "Fetching weather forecasts…");

    EpCityList cities = energy_plan_store_load_cities();
    if (!cities.entries || cities.count == 0) {
        LOG_WARN(LOG_MOD, "No cities in registry, skipping weather fetch");
        free(cities.entries);
        return;
    }

    int ok = 0, fail = 0;

    for (int i = 0; i < cities.count; i++) {
        EpCityEntry* e = &cities.entries[i];

        /* CsvRow fields: key=city  tag=price_zone  f1=lat  f2=lon */
        char url[512];
        snprintf(url, sizeof(url), "http://%s:%s%s?lat=%.6f&lon=%.6f",
                 cfg->service_host, cfg->service_port, cfg->weather_url_path,
                 e->f1, e->f2);

        LOG_INFO(LOG_MOD, "Fetching weather for %s: %s", e->key, url);

        char* body = http_get(url, cfg->service_port, cfg->timeout_ms);
        if (!body) {
            LOG_WARN(LOG_MOD, "HTTP failed for %s", e->key);
            fail++;
            continue;
        }

        json_error_t err;
        json_t*      root = json_loads(body, 0, &err);
        free(body);

        if (!root) {
            LOG_WARN(LOG_MOD, "JSON parse error for %s: %s", e->key, err.text);
            fail++;
            continue;
        }

        if (energy_plan_store_save_weather(e->key, e->f1, e->f2, root) != 0) {
            LOG_WARN(LOG_MOD, "Failed to save weather for %s", e->key);
            fail++;
        } else {
            ok++;
        }

        json_decref(root);
    }

    free(cities.entries);
    LOG_INFO(LOG_MOD, "Weather fetch done: %d ok, %d failed", ok, fail);
}

static void fetch_elpris(const FetchSchedulerConfig* cfg) {
    LOG_INFO(LOG_MOD, "Fetching elpris prices…");

    json_t* merged = json_array();
    if (!merged) {
        LOG_WARN(LOG_MOD, "json_array() allocation failed");
        return;
    }

    int ok = 0;

    for (int i = 0; i < cfg->price_zones_count; i++) {
        const char* zone = cfg->price_zones[i];

        char url[256];
        snprintf(url, sizeof(url), "http://%s:%s%s?price=%s", cfg->service_host,
                 cfg->service_port, cfg->elpris_url_path, zone);

        LOG_INFO(LOG_MOD, "Fetching elpris for %s: %s", zone, url);

        char* body = http_get(url, cfg->service_port, cfg->timeout_ms);
        if (!body) {
            LOG_WARN(LOG_MOD, "HTTP failed for elpris %s", zone);
            continue;
        }

        json_error_t err;
        json_t*      root = json_loads(body, 0, &err);
        free(body);

        if (!root) {
            LOG_WARN(LOG_MOD, "JSON parse error for elpris %s: %s", zone,
                     err.text);
            continue;
        }

        if (json_is_array(root)) {
            size_t  j;
            json_t* v;
            json_array_foreach(root, j, v) json_array_append(merged, v);
            ok++;
        } else if (json_is_object(root)) {
            json_array_append(merged, root);
            ok++;
        } else {
            LOG_WARN(LOG_MOD, "Unexpected JSON type for elpris %s", zone);
        }

        json_decref(root);
    }

    if (ok > 0) {
        if (energy_plan_store_save_elpris(merged) != 0) {
            LOG_WARN(LOG_MOD, "Failed to save merged elpris");
        } else {
            LOG_INFO(LOG_MOD, "Elpris saved (%d zones merged)", ok);
        }
    } else {
        LOG_WARN(LOG_MOD, "No elpris zones fetched successfully");
    }

    json_decref(merged);
}

static const FetchSchedulerConfig* g_cfg = NULL;

static void on_weather_timer(void) {
    if (!g_cfg) {
        return;
    }
    fetch_weather(g_cfg);
    fork_compute(g_cfg->compute_exe);
}

static void on_elpris_timer(void) {
    if (!g_cfg) {
        return;
    }
    fetch_elpris(g_cfg);
    fork_compute(g_cfg->compute_exe);
}

typedef struct {
    volatile sig_atomic_t*      shutdown;
    const FetchSchedulerConfig* cfg;
} SchedCtx;

static void* scheduler_thread_fn(void* arg) {
    SchedCtx* ctx = (SchedCtx*)arg;
    g_cfg         = ctx->cfg;

    SchedulerTimer* weather_timer =
        create_aligned_timer_utc(ctx->cfg->weather_interval_ms,
                                 ctx->cfg->weather_offset_ms, on_weather_timer);

    SchedulerTimer* elpris_timer =
        create_daily_timer(ctx->cfg->elpris_hour_utc,
                           ctx->cfg->elpris_minute_utc, 0, on_elpris_timer);

    SchedulerTimer* timers[] = {weather_timer, elpris_timer};
    run_scheduler(timers, 2, ctx->shutdown);

    destroy_timer(weather_timer);
    destroy_timer(elpris_timer);

    free(ctx);
    g_cfg = NULL;
    return NULL;
}

int fetch_scheduler_start(pthread_t*                  thread,
                          const FetchSchedulerConfig* config) {
    if (!thread || !config || !config->shutdown_flag || !config->service_host ||
        !config->service_port || !config->weather_url_path ||
        !config->elpris_url_path || !config->price_zones ||
        config->price_zones_count <= 0) {
        return -1;
    }

    // fetch on startup so that data is avalaiube right away
    fetch_weather(config);
    fetch_elpris(config);
    fork_compute(config->compute_exe);

    SchedCtx* ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -1;
    }
    ctx->shutdown = config->shutdown_flag;
    ctx->cfg      = config;

    if (pthread_create(thread, NULL, scheduler_thread_fn, ctx) != 0) {
        free(ctx);
        return -1;
    }
    return 0;
}

int fetch_scheduler_stop(pthread_t thread) {
    return pthread_join(thread, NULL);
}
