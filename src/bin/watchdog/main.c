/**
 * @file main.c
 * @brief Watchdog daemon for just-api.
 *
 * Responsibilities of this file:
 *   - Parse CLI arguments.
 *   - Resolve and validate all paths.
 *   - Load config and initialise subsystems.
 *   - Optionally daemonize.
 *   - Run the monitor/restart loop.
 *   - Tear everything down cleanly on exit.
 *
 * Actual logic lives in:
 *   daemon.c          – daemonization, PID file, signal setup
 *   process.c         – spawn, probe, and monitor the server child
 *   restart_policy.c  – backoff and restart-rate limiting
 */

#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "../../watchdog/daemon.h"
#include "../../watchdog/process.h"
#include "../../watchdog/restart_policy.h"
#include "config/config_parser.h"
#include "energy_plan/energy_plan_store.h"
#include "energy_plan/fetch_scheduler.h"
#include "logger/logger.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CFG_DEFAULT_SERVER_PATH "./just-server"
#define CFG_DEFAULT_COMPUTE_PATH "./compute"
#define CFG_DEFAULT_PID_FILE "/tmp/watchdog.pid"
#define CFG_DEFAULT_LOG_DIR "./logs"

// Timeout used when joining the scheduler thread on shutdown (seconds).
#define SCHED_JOIN_TIMEOUT_SEC 10

typedef struct {
    const char* server_path;
    const char* compute_path;
    const char* pid_file;
    const char* log_dir;
    int         foreground;
} WatchdogArgs;

static volatile sig_atomic_t g_shutdown   = 0;
static pid_t                 g_server_pid = -1;

// Joins the scheduler thread with a deadline.  Returns 0 on clean join,
// -1 if the thread did not finish within SCHED_JOIN_TIMEOUT_SEC.
static int join_scheduler(pthread_t thread) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += SCHED_JOIN_TIMEOUT_SEC;

    int rc = pthread_timedjoin_np(thread, NULL, &deadline);
    if (rc != 0) {
        LOG_WARN("WATCHDOG",
                 "Scheduler thread did not exit within %ds – "
                 "abandoning join",
                 SCHED_JOIN_TIMEOUT_SEC);
        return -1;
    }
    return 0;
}

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n\n"
           "  -s, --server PATH    Server binary     (default: %s)\n"
           "  -c, --compute PATH   Compute binary    (default: %s)\n"
           "  -p, --pid PATH       PID file          (default: %s)\n"
           "  -l, --log-dir PATH   Log directory     (default: %s)\n"
           "  -f, --foreground     Run in foreground\n"
           "  -h, --help           Show this help\n",
           prog, CFG_DEFAULT_SERVER_PATH, CFG_DEFAULT_COMPUTE_PATH,
           CFG_DEFAULT_PID_FILE, CFG_DEFAULT_LOG_DIR);
}

static void parse_args(int argc, char* argv[], WatchdogArgs* args) {
    static struct option opts[] = {{"server", required_argument, 0, 's'},
                                   {"compute", required_argument, 0, 'c'},
                                   {"pid", required_argument, 0, 'p'},
                                   {"log-dir", required_argument, 0, 'l'},
                                   {"foreground", no_argument, 0, 'f'},
                                   {"help", no_argument, 0, 'h'},
                                   {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "s:c:p:l:fh", opts, NULL)) != -1) {
        switch (opt) {
        case 's':
            args->server_path = optarg;
            break;
        case 'c':
            args->compute_path = optarg;
            break;
        case 'p':
            args->pid_file = optarg;
            break;
        case 'l':
            args->log_dir = optarg;
            break;
        case 'f':
            args->foreground = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            exit(0);
        default:
            print_usage(argv[0]);
            exit(1);
        }
    }
}

// Resolves `path` to an absolute path in `out` (size PATH_MAX).
// Creates the directory first if `create_if_missing` is set.
// Returns 0 on success, -1 on error.
static int resolve_dir(const char* path, char* out, int create_if_missing) {
    if (realpath(path, out) != NULL) {
        return 0;
    }
    if (!create_if_missing) {
        return -1;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return (realpath(path, out) != NULL) ? 0 : -1;
}

// Resolves an executable path; logs and returns -1 if not executable.
static int resolve_exe(const char* path, char* out, const char* label) {
    if (access(path, X_OK) != 0) {
        LOG_WARN("WATCHDOG", "%s binary not executable: %s", label, path);
        return -1;
    }
    if (realpath(path, out) == NULL) {
        LOG_WARN("WATCHDOG", "Cannot resolve %s path: %s", label, path);
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    WatchdogArgs args = {
        .server_path  = CFG_DEFAULT_SERVER_PATH,
        .compute_path = CFG_DEFAULT_COMPUTE_PATH,
        .pid_file     = CFG_DEFAULT_PID_FILE,
        .log_dir      = CFG_DEFAULT_LOG_DIR,
        .foreground   = 0,
    };
    parse_args(argc, argv, &args);

    static char abs_log_dir[PATH_MAX];
    if (resolve_dir(args.log_dir, abs_log_dir, 1) != 0) {
        fprintf(stderr, "Cannot resolve log dir: %s\n", args.log_dir);
        return 1;
    }
    args.log_dir = abs_log_dir;

    if (logger_init(args.log_dir, LOG_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialise logger\n");
        return 1;
    }
    LOG_INFO("WATCHDOG", "Watchdog starting (foreground=%d)", args.foreground);

    static char abs_base_dir[PATH_MAX];
    if (realpath(".", abs_base_dir) == NULL) {
        LOG_WARN("WATCHDOG", "Cannot resolve base directory");
        logger_shutdown();
        return 1;
    }

    ServerConfig cfg;
    if (config_parser_load("config.json", &cfg) != 0) {
        LOG_WARN("WATCHDOG", "Failed to load config.json, using defaults");
        config_set_defaults(&cfg);
    }

    // check exes exist i mean binaries ;)-D
    static char abs_server[PATH_MAX];
    if (resolve_exe(args.server_path, abs_server, "server") != 0) {
        logger_shutdown();
        return 1;
    }
    args.server_path = abs_server;

    static char abs_compute[PATH_MAX];
    if (args.compute_path) {
        if (resolve_exe(args.compute_path, abs_compute, "compute") != 0) {
            logger_shutdown();
            return 1;
        }
        args.compute_path = abs_compute;
    }

    EpStoreConfig store_cfg = {
        .base_dir   = cfg.energy_plan.base_dir,
        .max_cities = cfg.energy_plan.max_cities,
    };
    if (energy_plan_store_init(&store_cfg) != 0) {
        LOG_WARN("WATCHDOG", "Failed to initialise energy plan store");
        logger_shutdown();
        return 1;
    }

    // daemonize yo
    if (!args.foreground) {
        LOG_INFO("DAEMON", "Daemonizing…");
        if (daemon_daemonize() < 0) {
            LOG_WARN("DAEMON", "Failed to daemonize: %s", strerror(errno));
            energy_plan_store_shutdown();
            logger_shutdown();
            return 1;
        }
        // restoring working dir beacuse we still use fukin realtive dirs in
        // some code
        if (chdir(abs_base_dir) < 0) {
            LOG_WARN("DAEMON", "chdir(%s) failed: %s", abs_base_dir,
                     strerror(errno));
            energy_plan_store_shutdown();
            logger_shutdown();
            return 1;
        }

        if (daemon_write_pid_file(cfg.watchdog.pid_file) < 0) {
            LOG_WARN("DAEMON", "Failed to write PID file %s",
                     cfg.watchdog.pid_file);
            energy_plan_store_shutdown();
            logger_shutdown();
            return 1;
        }
    }

    daemon_setup_signals(&g_shutdown, &g_server_pid);

    RestartPolicyConfig rp_cfg = {
        .max_restarts       = cfg.watchdog.max_restarts,
        .restart_window_sec = cfg.watchdog.restart_window_sec,
        .initial_backoff_ms = cfg.watchdog.initial_backoff_ms,
        .max_backoff_ms     = cfg.watchdog.max_backoff_ms,
    };
    RestartPolicyState rp_state;
    restart_policy_init(&rp_state, &rp_cfg);

    g_server_pid =
        process_spawn_server(args.server_path, abs_log_dir, abs_base_dir);
    if (g_server_pid < 0) {
        LOG_WARN("WATCHDOG", "Failed to spawn server on startup");
        if (!args.foreground) {
            daemon_remove_pid_file(cfg.watchdog.pid_file);
        }
        energy_plan_store_shutdown();
        logger_shutdown();
        return 1;
    }
    // waiting for server to start beacuse we dont want fetch scheduler to start
    // tyring to fetch before the server is ready
    process_wait_for_server(cfg.scheduler.service_host, cfg.server_port,
                            cfg.watchdog.server_ready_wait_ms);

    // fetch scheduler startup
    const char* price_zone_ptrs[4];
    for (int i = 0; i < cfg.scheduler.price_zones_count; i++) {
        price_zone_ptrs[i] = cfg.scheduler.price_zones[i];
    }

    char port_str[9];
    snprintf(port_str, sizeof(port_str), "%d", cfg.server_port);

    pthread_t            sched_thread;
    FetchSchedulerConfig sched_cfg = {
        .shutdown_flag       = &g_shutdown,
        .compute_exe         = args.compute_path,
        .service_host        = cfg.scheduler.service_host,
        .service_port        = port_str,
        .weather_url_path    = cfg.scheduler.weather_url_path,
        .elpris_url_path     = cfg.scheduler.elpris_url_path,
        .price_zones         = price_zone_ptrs,
        .price_zones_count   = cfg.scheduler.price_zones_count,
        .timeout_ms          = cfg.scheduler.timeout_ms,
        .weather_interval_ms = cfg.scheduler.weather_interval_ms,
        .weather_offset_ms   = cfg.scheduler.weather_offset_ms,
        .elpris_hour_utc     = cfg.scheduler.elpris_hour_utc,
        .elpris_minute_utc   = cfg.scheduler.elpris_minute_utc,
    };

    if (fetch_scheduler_start(&sched_thread, &sched_cfg) != 0) {
        LOG_WARN("WATCHDOG", "fetch_scheduler_start failed");
        kill(-g_server_pid, SIGTERM);
        if (!args.foreground) {
            daemon_remove_pid_file(cfg.watchdog.pid_file);
        }
        energy_plan_store_shutdown();
        logger_shutdown();
        return 1;
    }

    LOG_INFO("WATCHDOG", "Entering main loop");

    struct timespec hc_last_ts    = {0, 0};
    int             hc_fail_count = 0;
    struct timespec sigterm_ts    = {0, 0};

    while (!g_shutdown) {
        if (g_server_pid <= 0) {
            g_server_pid = process_spawn_server(args.server_path, abs_log_dir,
                                                abs_base_dir);
            if (g_server_pid > 0) {
                process_wait_for_server(cfg.scheduler.service_host,
                                        cfg.server_port,
                                        cfg.watchdog.server_ready_wait_ms);
                clock_gettime(CLOCK_MONOTONIC, &hc_last_ts);
                hc_fail_count = 0;
                sigterm_ts    = (struct timespec){0, 0};
            }
        }

        /* SIGKILL fallback: if server did not exit after SIGTERM */
        if (sigterm_ts.tv_sec != 0 && g_server_pid > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (now.tv_sec - sigterm_ts.tv_sec) * 1000L +
                              (now.tv_nsec - sigterm_ts.tv_nsec) / 1000000L;
            if (elapsed_ms >= cfg.watchdog.sigkill_timeout_ms) {
                LOG_WARN("WATCHDOG",
                         "Server did not exit after SIGTERM (%ldms) – "
                         "sending SIGKILL",
                         elapsed_ms);
                kill(-g_server_pid, SIGKILL);
                sigterm_ts = (struct timespec){0, 0};
            }
        }

        int status = process_monitor(g_server_pid);

        if (status > 0) {
            // Server exited for any reason time for reastatr
            g_server_pid = -1;

            if (g_shutdown) {
                break;
            }

            if (!restart_policy_should_restart(&rp_state, &rp_cfg)) {
                LOG_WARN("WATCHDOG", "Too many restarts – giving up");
                g_shutdown = 1;
                break;
            }

            restart_policy_apply_backoff(&rp_state, &rp_cfg);
        } else if (g_server_pid > 0 &&
                   cfg.watchdog.health_check_interval_ms > 0 &&
                   sigterm_ts.tv_sec == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (now.tv_sec - hc_last_ts.tv_sec) * 1000L +
                              (now.tv_nsec - hc_last_ts.tv_nsec) / 1000000L;

            if (hc_last_ts.tv_sec == 0 ||
                elapsed_ms >= cfg.watchdog.health_check_interval_ms) {
                hc_last_ts = now;
                int hc     = process_health_check(
                    cfg.scheduler.service_host, cfg.server_port,
                    cfg.watchdog.health_check_timeout_ms);
                if (hc == 0) {
                    hc_fail_count = 0;
                } else {
                    hc_fail_count++;
                    LOG_WARN("WATCHDOG", "Health check failed (%d/%d)",
                             hc_fail_count, cfg.watchdog.health_check_failures);

                    if (hc_fail_count >= cfg.watchdog.health_check_failures) {
                        LOG_WARN("WATCHDOG",
                                 "Server unresponsive – sending SIGTERM");
                        kill(-g_server_pid, SIGTERM);
                        clock_gettime(CLOCK_MONOTONIC, &sigterm_ts);
                        hc_fail_count = 0;
                    }
                }
            }
        }

        usleep((useconds_t)cfg.watchdog.monitor_poll_us);
    }

    // stop server
    if (g_server_pid > 0) {
        LOG_INFO("WATCHDOG", "Sending SIGTERM to server process group %d",
                 g_server_pid);
        kill(-g_server_pid, SIGTERM);

        /* Poll with WNOHANG; send SIGKILL if server does not exit in time. */
        struct timespec stop_start, stop_now;
        clock_gettime(CLOCK_MONOTONIC, &stop_start);
        int wstatus;
        for (;;) {
            if (waitpid(g_server_pid, &wstatus, WNOHANG) != 0) {
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &stop_now);
            long elapsed_ms =
                (stop_now.tv_sec - stop_start.tv_sec) * 1000L +
                (stop_now.tv_nsec - stop_start.tv_nsec) / 1000000L;
            if (elapsed_ms >= cfg.watchdog.sigkill_timeout_ms) {
                LOG_WARN("WATCHDOG",
                         "Server did not exit after SIGTERM (%ldms) – "
                         "sending SIGKILL",
                         elapsed_ms);
                kill(-g_server_pid, SIGKILL);
                waitpid(g_server_pid, &wstatus, 0);
                break;
            }
            usleep(50 * 1000); /* 50 ms poll */
        }
        g_server_pid = -1;
    }

    if (join_scheduler(sched_thread) != 0) {
        LOG_WARN("WATCHDOG", "Scheduler thread join failed");
    }

    if (!args.foreground) {
        daemon_remove_pid_file(cfg.watchdog.pid_file);
    }
    energy_plan_store_shutdown();
    LOG_INFO("WATCHDOG", "Watchdog stopped");
    logger_shutdown();
    return 0;
}
