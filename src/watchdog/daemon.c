#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "daemon.h"

#include "logger/logger.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t* g_shutdown_flag  = NULL;
static pid_t*                 g_server_pid_ptr = NULL;

static void signal_handler(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        if (g_shutdown_flag) {
            *g_shutdown_flag = 1;
        }
        // Forward SIGTERM to server so it shutdown
        if (g_server_pid_ptr && *g_server_pid_ptr > 0) {
            kill(-(*g_server_pid_ptr), SIGTERM);
        }
    }
}

int daemon_daemonize(void) {
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

    // Redirect stdio to /dev/null.
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);

    return 0;
}

int daemon_write_pid_file(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        LOG_WARN("DAEMON", "Cannot open PID file %s: %s", path,
                 strerror(errno));
        return -1;
    }

    char buf[32];
    int  len = snprintf(buf, sizeof(buf), "%d\n", getpid());
    if (write(fd, buf, (size_t)len) != len) {
        LOG_WARN("DAEMON", "Short write to PID file %s: %s", path,
                 strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

void daemon_remove_pid_file(const char* path) { unlink(path); }

void daemon_setup_signals(volatile sig_atomic_t* shutdown_flag,
                          pid_t*                 server_pid_ptr) {
    g_shutdown_flag  = shutdown_flag;
    g_server_pid_ptr = server_pid_ptr;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    signal(SIGCHLD, SIG_DFL);
}
