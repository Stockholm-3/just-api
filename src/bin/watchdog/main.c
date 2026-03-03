/**
 * @file watchdog.c
 * @brief Watchdog daemon for just-weather-server.
 *
 * Monitors the server process and restarts it on crash with exponential
 * backoff. Initialises the energy-plan store and starts the fetch scheduler
 * once the server is ready.
 *
 * All configuration is taken from #defines below. Replace these with config
 * file loading when ready.
 */

#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "energy_plan/energy_plan_store.h"
#include "energy_plan/fetch_scheduler.h"
#include "logger/logger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CFG_DEFAULT_SERVER_PATH "./just-server"
#define CFG_DEFAULT_COMPUTE_PATH "./compute"
#define CFG_DEFAULT_PID_FILE "/tmp/watchdog.pid"
#define CFG_DEFAULT_LOG_DIR "./logs"
#define CFG_ENERGY_PLAN_BASE_DIR "energy_plan"

#define CFG_SERVICE_HOST "127.0.0.1"
#define CFG_SERVICE_PORT "10680"
#define CFG_SERVICE_PORT_INT 10680
#define CFG_HTTP_TIMEOUT_MS 10000UL

#define CFG_WEATHER_URL_PATH "/v1/forecast/minutely"
#define CFG_ELPRIS_URL_PATH "/v1/elpris"

static const char* const CFG_PRICE_ZONES[] = {"SE1", "SE2", "SE3", "SE4"};
#define CFG_PRICE_ZONES_COUNT                                                  \
    (int)(sizeof(CFG_PRICE_ZONES) / sizeof(CFG_PRICE_ZONES[0]))

#define CFG_WEATHER_INTERVAL_MS (60ULL * 60 * 1000) /* 1 hour           */
#define CFG_WEATHER_OFFSET_MS (60ULL * 2 * 1000)    /* at HH:02:00 UTC  */
#define CFG_ELPRIS_HOUR_UTC 13                      /* daily at 13:05   */
#define CFG_ELPRIS_MINUTE_UTC 5

/* City registry */
#define CFG_MAX_CITIES 200
#define CFG_CITY_TTL_SECONDS (2UL * 24 * 3600)

/* Watchdog restart policy */
#define CFG_MAX_RESTARTS 10
#define CFG_RESTART_WINDOW_SEC 60
#define CFG_INITIAL_BACKOFF_MS 1000
#define CFG_MAX_BACKOFF_MS 30000
#define CFG_SERVER_READY_WAIT_MS 10000
#define CFG_MONITOR_POLL_US 100000 /* 100 ms */

typedef struct {
    const char* server_path;
    const char* compute_path;
    const char* pid_file;
    const char* log_dir;
    int         foreground;
} WatchdogArgs;

typedef struct {
    pid_t  server_pid;
    int    restart_count;
    time_t last_restart_window_start;
    int    current_backoff_ms;
} WatchdogState;

static volatile sig_atomic_t g_shutdown = 0;
static WatchdogState         g_state    = {0};
static const char*           g_log_dir  = NULL;
static const char*           g_base_dir = NULL;

static void on_signal(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        g_shutdown = 1;
        if (g_state.server_pid > 0) {
            kill(g_state.server_pid, SIGTERM);
        }
    }
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGCHLD, SIG_DFL);
}

static int daemonize(void) {
    pid_t pid;
    if ((pid = fork()) < 0) {
        return -1;
    }
    if (pid > 0) {
        _exit(0);
    }
    if (setsid() < 0) {
        return -1;
    }
    if ((pid = fork()) < 0) {
        return -1;
    }
    if (pid > 0) {
        _exit(0);
    }

    umask(0);
    if (chdir("/") < 0) {
        return -1;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
    return 0;
}

static int write_pid_file(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    char buf[32];
    int  len = snprintf(buf, sizeof(buf), "%d\n", getpid());
    if (write(fd, buf, (size_t)len) != len) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static void remove_pid_file(const char* path) { unlink(path); }

static pid_t spawn_server(const char* server_path) {
    pid_t pid = fork();
    if (pid < 0) {
        LOG_WARN("WATCHDOG", "fork() failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execl(server_path, server_path, "--log-dir", g_log_dir, "--base-dir",
              g_base_dir, NULL);
        _exit(127);
    }
    LOG_INFO("WATCHDOG", "Server spawned: PID %d", pid);
    return pid;
}

static int monitor_server(void) {
    int   status;
    pid_t r = waitpid(g_state.server_pid, &status, WNOHANG);
    if (r == 0) {
        return 0;
    }
    if (r < 0) {
        if (errno == ECHILD) {
            LOG_WARN("WATCHDOG", "Server PID not found (ECHILD)");
            return 1;
        }
        LOG_WARN("WATCHDOG", "waitpid: %s", strerror(errno));
        return -1;
    }
    if (WIFEXITED(status)) {
        LOG_INFO("WATCHDOG", "Server exited: code %d", WEXITSTATUS(status));
        return WEXITSTATUS(status) == 0 ? -1 : 1;
    }
    if (WIFSIGNALED(status)) {
        LOG_WARN("WATCHDOG", "Server killed by signal %d", WTERMSIG(status));
    }
    return 1;
}

static int should_restart(void) {
    time_t now = time(NULL);
    if (now - g_state.last_restart_window_start > CFG_RESTART_WINDOW_SEC) {
        g_state.restart_count             = 0;
        g_state.last_restart_window_start = now;
        g_state.current_backoff_ms        = CFG_INITIAL_BACKOFF_MS;
    }
    if (g_state.restart_count >= CFG_MAX_RESTARTS) {
        LOG_WARN("WATCHDOG", "Max restarts (%d) reached in %d seconds",
                 CFG_MAX_RESTARTS, CFG_RESTART_WINDOW_SEC);
        return 0;
    }
    return 1;
}

static void apply_backoff(void) {
    LOG_INFO("WATCHDOG", "Backoff: %dms (attempt %d/%d)",
             g_state.current_backoff_ms, g_state.restart_count + 1,
             CFG_MAX_RESTARTS);
    usleep((useconds_t)g_state.current_backoff_ms * 1000);
    g_state.current_backoff_ms *= 2;
    if (g_state.current_backoff_ms > CFG_MAX_BACKOFF_MS) {
        g_state.current_backoff_ms = CFG_MAX_BACKOFF_MS;
    }
    g_state.restart_count++;
}

static void wait_for_server(const char* host, int port, int max_wait_ms) {
    int elapsed = 0;
    while (elapsed < max_wait_ms) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            break;
        }

        struct sockaddr_in addr = {0};
        addr.sin_family         = AF_INET;
        addr.sin_port           = htons((uint16_t)port);
        inet_pton(AF_INET, host, &addr.sin_addr);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            close(fd);
            LOG_INFO("WATCHDOG", "Server ready after %dms", elapsed);
            return;
        }
        close(fd);
        usleep(50 * 1000);
        elapsed += 50;
    }
    LOG_WARN("WATCHDOG", "Server not ready after %dms, proceeding anyway",
             max_wait_ms);
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
    int                  opt;
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
    if (realpath(args.log_dir, abs_log_dir) == NULL) {
        if (mkdir(args.log_dir, 0755) != 0 ||
            realpath(args.log_dir, abs_log_dir) == NULL) {
            fprintf(stderr, "Cannot resolve log dir: %s\n", args.log_dir);
            return 1;
        }
    }
    args.log_dir = abs_log_dir;
    g_log_dir    = abs_log_dir;

    static char abs_base_dir[PATH_MAX];
    if (realpath(".", abs_base_dir) == NULL) {
        fprintf(stderr, "Cannot resolve base directory\n");
        return 1;
    }
    g_base_dir = abs_base_dir;

    if (logger_init(args.log_dir, LOG_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialise logger\n");
        return 1;
    }
    LOG_INFO("WATCHDOG", "Watchdog starting…");

    if (access(args.server_path, X_OK) != 0) {
        LOG_WARN("WATCHDOG", "Server binary not executable: %s",
                 args.server_path);
        logger_shutdown();
        return 1;
    }
    static char abs_server[PATH_MAX];
    if (realpath(args.server_path, abs_server) == NULL) {
        LOG_WARN("WATCHDOG", "Cannot resolve server path: %s",
                 args.server_path);
        logger_shutdown();
        return 1;
    }
    args.server_path = abs_server;

    static char abs_compute[PATH_MAX];
    if (args.compute_path) {
        if (access(args.compute_path, X_OK) != 0) {
            LOG_WARN("WATCHDOG", "Compute binary not executable: %s",
                     args.compute_path);
            logger_shutdown();
            return 1;
        }
        if (realpath(args.compute_path, abs_compute) == NULL) {
            LOG_WARN("WATCHDOG", "Cannot resolve compute path: %s",
                     args.compute_path);
            logger_shutdown();
            return 1;
        }
        args.compute_path = abs_compute;
    }

    EpStoreConfig store_cfg = {
        .base_dir   = CFG_ENERGY_PLAN_BASE_DIR,
        .max_cities = CFG_MAX_CITIES,
    };
    if (energy_plan_store_init(&store_cfg) != 0) {
        LOG_WARN("WATCHDOG", "Failed to initialise energy plan store");
        logger_shutdown();
        return 1;
    }

    if (!args.foreground) {
        LOG_INFO("DAEMON", "Daemonizing…");
        if (daemonize() < 0) {
            LOG_WARN("DAEMON", "Failed to daemonize");
            energy_plan_store_shutdown();
            logger_shutdown();
            return 1;
        }
        // Restore working directory after daemonize(). should be changed
        if (chdir(abs_base_dir) < 0) {
            LOG_WARN("DAEMON", "chdir(%s) failed: %s", abs_base_dir,
                     strerror(errno));
            energy_plan_store_shutdown();
            logger_shutdown();
            return 1;
        }
    }

    if (write_pid_file(args.pid_file) < 0) {
        energy_plan_store_shutdown();
        logger_shutdown();
        return 1;
    }

    setup_signals();

    g_state.server_pid                = -1;
    g_state.restart_count             = 0;
    g_state.last_restart_window_start = time(NULL);
    g_state.current_backoff_ms        = CFG_INITIAL_BACKOFF_MS;

    g_state.server_pid = spawn_server(args.server_path);
    if (g_state.server_pid < 0) {
        LOG_WARN("WATCHDOG", "Failed to spawn server on startup");
        energy_plan_store_shutdown();
        logger_shutdown();
        return 1;
    }
    wait_for_server(CFG_SERVICE_HOST, CFG_SERVICE_PORT_INT,
                    CFG_SERVER_READY_WAIT_MS);

    /* Start the fetch scheduler. */
    pthread_t            sched_thread;
    FetchSchedulerConfig sched_cfg = {
        .shutdown_flag       = &g_shutdown,
        .compute_exe         = args.compute_path,
        .service_host        = CFG_SERVICE_HOST,
        .service_port        = CFG_SERVICE_PORT,
        .weather_url_path    = CFG_WEATHER_URL_PATH,
        .elpris_url_path     = CFG_ELPRIS_URL_PATH,
        .price_zones         = CFG_PRICE_ZONES,
        .price_zones_count   = CFG_PRICE_ZONES_COUNT,
        .timeout_ms          = CFG_HTTP_TIMEOUT_MS,
        .weather_interval_ms = CFG_WEATHER_INTERVAL_MS,
        .weather_offset_ms   = CFG_WEATHER_OFFSET_MS,
        .elpris_hour_utc     = CFG_ELPRIS_HOUR_UTC,
        .elpris_minute_utc   = CFG_ELPRIS_MINUTE_UTC,
    };

    if (fetch_scheduler_start(&sched_thread, &sched_cfg) != 0) {
        LOG_WARN("WATCHDOG", "fetch_scheduler_start failed");
        kill(g_state.server_pid, SIGTERM);
        energy_plan_store_shutdown();
        logger_shutdown();
        return 1;
    }

    LOG_INFO("WATCHDOG", "Entering main loop");

    while (!g_shutdown) {
        if (g_state.server_pid <= 0) {
            g_state.server_pid = spawn_server(args.server_path);
            if (g_state.server_pid > 0) {
                wait_for_server(CFG_SERVICE_HOST, CFG_SERVICE_PORT_INT,
                                CFG_SERVER_READY_WAIT_MS);
            }
        }

        int status = monitor_server();
        if (status > 0) {
            g_state.server_pid = -1;
            if (!g_shutdown && should_restart()) {
                apply_backoff();
            } else if (!g_shutdown) {
                LOG_WARN("WATCHDOG", "Too many restarts – giving up");
                break;
            }
        } else if (status < 0) {
            LOG_INFO("WATCHDOG", "Server exited cleanly");
            break;
        }

        usleep(CFG_MONITOR_POLL_US);
    }

    if (g_state.server_pid > 0) {
        LOG_INFO("WATCHDOG", "Stopping server (PID %d)…", g_state.server_pid);
        int status;
        kill(g_state.server_pid, SIGTERM);
        waitpid(g_state.server_pid, &status, 0);
    }

    if (fetch_scheduler_stop(sched_thread) != 0) {
        LOG_WARN("WATCHDOG", "Scheduler thread join failed");
    }

    remove_pid_file(args.pid_file);
    energy_plan_store_shutdown();
    LOG_INFO("WATCHDOG", "Watchdog stopped");
    logger_shutdown();
    return 0;
}
