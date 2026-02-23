/**
 * watchdog.c - Watchdog daemon for just-weather-server
 *
 * Monitors the server process and restarts it on crash.
 * Implements exponential backoff for restart attempts.
 */

#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#    include "netinet/in.h"
#    include "sys/socket.h"
#endif

#include "../logger/logger.h"
#include "energy_plan/fetch_scheduler.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <scheduler.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SERVER_PATH "./just-server"
#define DEFAULT_PID_FILE "/tmp/watchdog.pid"
#define DEFAULT_LOG_DIR "./logs"

#define MAX_RESTARTS 10
#define RESTART_WINDOW_SEC 60
#define INITIAL_BACKOFF_MS 1000
#define MAX_BACKOFF_MS 30000

typedef struct {
    const char* server_path;
    const char* pid_file;
    const char* log_dir;
    int         foreground;
} WatchdogConfig;

typedef struct {
    pid_t  server_pid;
    int    restart_count;
    time_t last_restart_window_start;
    int    current_backoff_ms;
} WatchdogState;

static volatile sig_atomic_t g_shutdown_requested = 0;
static WatchdogState         g_state              = {0};
static const char*           g_log_dir            = NULL;
static const char*           g_base_dir           = NULL;

static void watchdog_signal_handler(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        g_shutdown_requested = 1;
        if (g_state.server_pid > 0) {
            kill(g_state.server_pid, SIGTERM);
        }
    }
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = watchdog_signal_handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    signal(SIGCHLD, SIG_DFL);
}

static int daemonize(void) {
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        _exit(0);
    }

    if (setsid() < 0) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
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
        LOG_ERROR("DAEMON", "Failed to create PID file: %s", path);
        return -1;
    }

    char buf[32];
    int  len = snprintf(buf, sizeof(buf), "%d\n", getpid());
    if (write(fd, buf, (size_t)len) != len) {
        LOG_ERROR("DAEMON", "Failed to write PID file: %s", path);
        close(fd);
        return -1;
    }
    close(fd);
    LOG_DEBUG("DAEMON", "PID file written: %s", path);
    return 0;
}

static void remove_pid_file(const char* path) {
    unlink(path);
    LOG_DEBUG("DAEMON", "PID file removed: %s", path);
}

static pid_t spawn_server(const char* server_path) {
    pid_t pid = fork();

    if (pid < 0) {
        LOG_ERROR("RESTART", "Failed to fork server process: %s",
                  strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // Child: exec the server with log directory
        execl(server_path, server_path, "--log-dir", g_log_dir, "--base-dir",
              g_base_dir, NULL);
        _exit(127);
    }

    LOG_INFO("RESTART", "Server spawned with PID %d", pid);
    return pid;
}

static int monitor_server(void) {
    int   status;
    pid_t result = waitpid(g_state.server_pid, &status, WNOHANG);

    if (result == 0) {
        return 0;
    }

    if (result < 0) {
        if (errno == ECHILD) {
            LOG_WARN("MONITOR", "Server process not found (ECHILD)");
            return 1;
        }
        LOG_ERROR("MONITOR", "waitpid failed: %s", strerror(errno));
        return -1;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        LOG_INFO("MONITOR", "Server exited with code %d", code);
        if (code == 0) {
            return -1;
        }
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        LOG_WARN("MONITOR", "Server killed by signal %d", sig);
    }

    return 1;
}

static int should_restart(void) {
    time_t now = time(NULL);

    if (now - g_state.last_restart_window_start > RESTART_WINDOW_SEC) {
        g_state.restart_count             = 0;
        g_state.last_restart_window_start = now;
        g_state.current_backoff_ms        = INITIAL_BACKOFF_MS;
        LOG_DEBUG("RESTART", "Restart window reset");
    }

    if (g_state.restart_count >= MAX_RESTARTS) {
        LOG_ERROR("RESTART", "Max restarts (%d) reached in %d seconds",
                  MAX_RESTARTS, RESTART_WINDOW_SEC);
        return 0;
    }

    return 1;
}

static void apply_backoff(void) {
    LOG_INFO("RESTART", "Applying backoff: %d ms (attempt %d/%d)",
             g_state.current_backoff_ms, g_state.restart_count + 1,
             MAX_RESTARTS);

    usleep((useconds_t)g_state.current_backoff_ms * 1000);

    g_state.current_backoff_ms *= 2;
    if (g_state.current_backoff_ms > MAX_BACKOFF_MS) {
        g_state.current_backoff_ms = MAX_BACKOFF_MS;
    }
    g_state.restart_count++;
}

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nOptions:\n");
    printf("  -s, --server PATH   Path to server binary (default: %s)\n",
           DEFAULT_SERVER_PATH);
    printf("  -p, --pid PATH      PID file path (default: %s)\n",
           DEFAULT_PID_FILE);
    printf("  -l, --log-dir PATH  Log directory (default: %s)\n",
           DEFAULT_LOG_DIR);
    printf("  -f, --foreground    Run in foreground (don't daemonize)\n");
    printf("  -h, --help          Show this help\n");
}

static void parse_args(int argc, char* argv[], WatchdogConfig* config) {
    static struct option long_options[] = {
        {"server", required_argument, 0, 's'},
        {"pid", required_argument, 0, 'p'},
        {"log-dir", required_argument, 0, 'l'},
        {"foreground", no_argument, 0, 'f'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "s:p:l:fh", long_options, NULL)) !=
           -1) {
        switch (opt) {
        case 's':
            config->server_path = optarg;
            break;
        case 'p':
            config->pid_file = optarg;
            break;
        case 'l':
            config->log_dir = optarg;
            break;
        case 'f':
            config->foreground = 1;
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

int main(int argc, char* argv[]) {
    WatchdogConfig config = {
        .server_path = DEFAULT_SERVER_PATH,
        .pid_file    = DEFAULT_PID_FILE,
        .log_dir     = DEFAULT_LOG_DIR,
        .foreground  = 0,
    };

    parse_args(argc, argv, &config);

    static char abs_log_dir[PATH_MAX];
    if (realpath(config.log_dir, abs_log_dir) == NULL) {
        if (mkdir(config.log_dir, 0755) == 0) {
            if (realpath(config.log_dir, abs_log_dir) == NULL) {
                fprintf(stderr, "Cannot resolve log directory: %s\n",
                        config.log_dir);
                return 1;
            }
        } else {
            fprintf(stderr, "Cannot create log directory: %s\n",
                    config.log_dir);
            return 1;
        }
    }
    config.log_dir = abs_log_dir;
    g_log_dir      = abs_log_dir;

    static char abs_base_dir[PATH_MAX];
    if (realpath(".", abs_base_dir) == NULL) {
        fprintf(stderr, "Cannot resolve base directory\n");
        return 1;
    }
    g_base_dir = abs_base_dir;

    if (logger_init(config.log_dir, LOG_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    LOG_INFO("WATCHDOG", "Watchdog starting...");

    if (access(config.server_path, X_OK) != 0) {
        LOG_ERROR("WATCHDOG", "Server binary not found or not executable: %s",
                  config.server_path);
        logger_shutdown();
        return 1;
    }

    static char abs_server_path[PATH_MAX];
    if (realpath(config.server_path, abs_server_path) == NULL) {
        LOG_ERROR("WATCHDOG", "Cannot resolve path: %s", config.server_path);
        logger_shutdown();
        return 1;
    }
    config.server_path = abs_server_path;
    LOG_DEBUG("WATCHDOG", "Server path resolved: %s", config.server_path);

    if (!config.foreground) {
        LOG_INFO("DAEMON", "Daemonizing...");
        if (daemonize() < 0) {
            LOG_ERROR("DAEMON", "Failed to daemonize");
            logger_shutdown();
            return 1;
        }
        // TODO: I added this for a quick fix for the fetch step which uses
        // relative paths. in the future we must change this back and make it
        // use absolute paths everywhere!
        if (chdir(abs_base_dir) < 0) {
            LOG_ERROR("DAEMON", "Failed to chdir to base dir: %s",
                      abs_base_dir);
            logger_shutdown();
            return 1;
        }
    }

    if (write_pid_file(config.pid_file) < 0) {
        logger_shutdown();
        return 1;
    }

    setup_signals();
    LOG_DEBUG("WATCHDOG", "Signal handlers configured");

    g_state.server_pid                = -1;
    g_state.restart_count             = 0;
    g_state.last_restart_window_start = time(NULL);
    g_state.current_backoff_ms        = INITIAL_BACKOFF_MS;

    // Spawn the server first, wait for it to be ready, then start the
    // scheduler so fetches never race against a server that isn't up yet.
    g_state.server_pid = spawn_server(config.server_path);
    if (g_state.server_pid < 0) {
        LOG_ERROR("WATCHDOG", "Failed to spawn server on startup");
        logger_shutdown();
        return 1;
    }

    wait_for_server("127.0.0.1", 10680, 10000);

    pthread_t scheduler_thread;

    SchedulerServiceConfig sched_cfg = {
        .shutdown_flag = &g_shutdown_requested,
    };

    if (fetch_scheduler_start(&scheduler_thread, &sched_cfg) != 0) {
        perror("fetch_scheduler_start");
        exit(EXIT_FAILURE);
    }

    LOG_INFO("WATCHDOG", "Entering main loop");

    while (!g_shutdown_requested) {
        if (g_state.server_pid <= 0) {
            g_state.server_pid = spawn_server(config.server_path);
            if (g_state.server_pid > 0) {
                wait_for_server("127.0.0.1", 10680, 10000);
            }
        }

        int status = monitor_server();

        if (status > 0) {
            g_state.server_pid = -1;

            if (!g_shutdown_requested && should_restart()) {
                apply_backoff();
            } else if (!g_shutdown_requested) {
                LOG_ERROR("WATCHDOG", "Giving up after too many restarts");
                break;
            }
        } else if (status < 0) {
            LOG_INFO("WATCHDOG", "Server exited cleanly, stopping watchdog");
            break;
        }

        usleep(100000);
    }

    if (g_state.server_pid > 0) {
        LOG_INFO("WATCHDOG", "Shutting down server (PID %d)...",
                 g_state.server_pid);
        int status;
        kill(g_state.server_pid, SIGTERM);
        waitpid(g_state.server_pid, &status, 0);
        LOG_INFO("WATCHDOG", "Server stopped");
    }

    if (fetch_scheduler_stop(scheduler_thread) != 0) {
        perror("pthread_join");
    }

    remove_pid_file(config.pid_file);

    LOG_INFO("WATCHDOG", "Watchdog stopped");
    logger_shutdown();

    return 0;
}
