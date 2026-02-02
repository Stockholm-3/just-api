/**
 * watchdog.c - Watchdog daemon for just-weather-server
 *
 * Monitors the server process and restarts it on crash.
 * Implements exponential backoff for restart attempts.
 */

#define _GNU_SOURCE

#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SERVER_PATH "./just-server"
#define DEFAULT_PID_FILE    "/tmp/watchdog.pid"
#define DEFAULT_LOG_DIR     "./logs"

#define MAX_RESTARTS       10
#define RESTART_WINDOW_SEC 60
#define INITIAL_BACKOFF_MS 1000
#define MAX_BACKOFF_MS     30000

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
static int                   g_server_stdout_fd   = -1;
static int                   g_server_stderr_fd   = -1;

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

static void close_server_pipes(void) {
    if (g_server_stdout_fd >= 0) {
        close(g_server_stdout_fd);
        g_server_stdout_fd = -1;
    }
    if (g_server_stderr_fd >= 0) {
        close(g_server_stderr_fd);
        g_server_stderr_fd = -1;
    }
}

static void log_lines(const char* buf, LogLevel level) {
    const char* line_start = buf;
    const char* p          = buf;

    while (*p) {
        if (*p == '\n') {
            size_t len = (size_t)(p - line_start);
            if (len > 0) {
                char line[1024];
                if (len >= sizeof(line)) {
                    len = sizeof(line) - 1;
                }
                memcpy(line, line_start, len);
                line[len] = '\0';
                logger_log(level, "SERVER", "%s", line);
            }
            line_start = p + 1;
        }
        p++;
    }

    // Логуємо залишок, якщо є
    if (*line_start != '\0') {
        logger_log(level, "SERVER", "%s", line_start);
    }
}

static void read_server_output(void) {
    char    buf[4096];
    ssize_t n;

    // Читаємо stdout
    while (g_server_stdout_fd >= 0 &&
           (n = read(g_server_stdout_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        log_lines(buf, LOG_INFO);
    }

    // Читаємо stderr
    while (g_server_stderr_fd >= 0 &&
           (n = read(g_server_stderr_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        log_lines(buf, LOG_WARN);
    }
}

static pid_t spawn_server(const char* server_path) {
    int stdout_pipe[2], stderr_pipe[2];

    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        LOG_ERROR("RESTART", "Failed to create pipes: %s", strerror(errno));
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        LOG_ERROR("RESTART", "Failed to fork server process: %s", strerror(errno));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        // Child: redirect stdout/stderr to pipes
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        execl(server_path, server_path, NULL);
        _exit(127);
    }

    // Parent: setup non-blocking reads
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    g_server_stdout_fd = stdout_pipe[0];
    g_server_stderr_fd = stderr_pipe[0];

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
             g_state.current_backoff_ms, g_state.restart_count + 1, MAX_RESTARTS);

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

int main(int argc, char* argv[]) {
    WatchdogConfig config = {
        .server_path = DEFAULT_SERVER_PATH,
        .pid_file    = DEFAULT_PID_FILE,
        .log_dir     = DEFAULT_LOG_DIR,
        .foreground  = 0,
    };

    parse_args(argc, argv, &config);

    // Convert log_dir to absolute path before daemonizing (chdir changes to /)
    static char abs_log_dir[PATH_MAX];
    if (realpath(config.log_dir, abs_log_dir) == NULL) {
        // Directory might not exist yet, try to create it
        if (mkdir(config.log_dir, 0755) == 0) {
            if (realpath(config.log_dir, abs_log_dir) == NULL) {
                fprintf(stderr, "Cannot resolve log directory: %s\n", config.log_dir);
                return 1;
            }
        } else {
            fprintf(stderr, "Cannot create log directory: %s\n", config.log_dir);
            return 1;
        }
    }
    config.log_dir = abs_log_dir;

    // Initialize logger before anything else
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

    // Convert to absolute path before daemonizing (chdir changes to /)
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

    LOG_INFO("WATCHDOG", "Entering main loop");

    while (!g_shutdown_requested) {
        if (g_state.server_pid <= 0) {
            g_state.server_pid = spawn_server(config.server_path);
        }

        // Читаємо вивід сервера
        read_server_output();

        int status = monitor_server();

        if (status > 0) {
            // Читаємо останній вивід перед закриттям
            read_server_output();
            close_server_pipes();
            g_state.server_pid = -1;

            if (!g_shutdown_requested && should_restart()) {
                apply_backoff();
            } else if (!g_shutdown_requested) {
                LOG_ERROR("WATCHDOG", "Giving up after too many restarts");
                break;
            }
        } else if (status < 0) {
            read_server_output();
            close_server_pipes();
            LOG_INFO("WATCHDOG", "Server exited cleanly, stopping watchdog");
            break;
        }

        usleep(100000);
    }

    if (g_state.server_pid > 0) {
        LOG_INFO("WATCHDOG", "Shutting down server (PID %d)...", g_state.server_pid);
        int status;
        kill(g_state.server_pid, SIGTERM);
        waitpid(g_state.server_pid, &status, 0);
        read_server_output();
        close_server_pipes();
        LOG_INFO("WATCHDOG", "Server stopped");
    }

    remove_pid_file(config.pid_file);

    LOG_INFO("WATCHDOG", "Watchdog stopped");
    logger_shutdown();

    return 0;
}
