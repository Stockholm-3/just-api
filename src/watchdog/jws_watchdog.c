/**
 * jws_watchdog.c - Watchdog daemon for just-weather-server
 *
 * Monitors the server process and restarts it on crash.
 * Implements exponential backoff for restart attempts.
 */

#define _GNU_SOURCE

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

#define DEFAULT_SERVER_PATH "./just-weather-server"
#define DEFAULT_PID_FILE "/tmp/jws-watchdog.pid"

#define MAX_RESTARTS 10
#define RESTART_WINDOW_SEC 60
#define INITIAL_BACKOFF_MS 1000
#define MAX_BACKOFF_MS 30000

typedef struct {
    const char* server_path;
    const char* pid_file;
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
        return -1;
    }

    if (pid == 0) {
        execl(server_path, server_path, NULL);
        _exit(127);
    }

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
            return 1;
        }
        return -1;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) {
            return -1;
        }
    }

    return 1;
}

static int should_restart(void) {
    time_t now = time(NULL);

    if (now - g_state.last_restart_window_start > RESTART_WINDOW_SEC) {
        g_state.restart_count             = 0;
        g_state.last_restart_window_start = now;
        g_state.current_backoff_ms        = INITIAL_BACKOFF_MS;
    }

    if (g_state.restart_count >= MAX_RESTARTS) {
        return 0;
    }

    return 1;
}

static void apply_backoff(void) {
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
    printf("  -f, --foreground    Run in foreground (don't daemonize)\n");
    printf("  -h, --help          Show this help\n");
}

static void parse_args(int argc, char* argv[], WatchdogConfig* config) {
    static struct option long_options[] = {
        {"server", required_argument, 0, 's'},
        {"pid", required_argument, 0, 'p'},
        {"foreground", no_argument, 0, 'f'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "s:p:fh", long_options, NULL)) !=
           -1) {
        switch (opt) {
        case 's':
            config->server_path = optarg;
            break;
        case 'p':
            config->pid_file = optarg;
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
        .foreground  = 0,
    };

    parse_args(argc, argv, &config);

    if (access(config.server_path, X_OK) != 0) {
        fprintf(stderr,
                "Error: Server binary not found or not executable: %s\n",
                config.server_path);
        return 1;
    }

    // Convert to absolute path before daemonizing (chdir changes to /)
    static char abs_server_path[PATH_MAX];
    if (realpath(config.server_path, abs_server_path) == NULL) {
        fprintf(stderr, "Error: Cannot resolve path: %s\n", config.server_path);
        return 1;
    }
    config.server_path = abs_server_path;

    if (!config.foreground) {
        printf("Starting watchdog daemon...\n");
        if (daemonize() < 0) {
            fprintf(stderr, "Failed to daemonize\n");
            return 1;
        }
    }

    if (write_pid_file(config.pid_file) < 0) {
        return 1;
    }

    setup_signals();

    g_state.server_pid                = -1;
    g_state.restart_count             = 0;
    g_state.last_restart_window_start = time(NULL);
    g_state.current_backoff_ms        = INITIAL_BACKOFF_MS;

    while (!g_shutdown_requested) {
        if (g_state.server_pid <= 0) {
            g_state.server_pid = spawn_server(config.server_path);
        }

        int status = monitor_server();

        if (status > 0) {
            g_state.server_pid = -1;

            if (!g_shutdown_requested && should_restart()) {
                apply_backoff();
            } else if (!g_shutdown_requested) {
                break;
            }
        } else if (status < 0) {
            break;
        }

        usleep(100000);
    }

    if (g_state.server_pid > 0) {
        int status;
        kill(g_state.server_pid, SIGTERM);
        waitpid(g_state.server_pid, &status, 0);
    }

    remove_pid_file(config.pid_file);

    return 0;
}
