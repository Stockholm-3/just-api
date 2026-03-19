#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "process.h"

#include "logger/logger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t process_spawn_server(const char* server_path, const char* log_dir,
                           const char* base_dir) {
    pid_t pid = fork();
    if (pid < 0) {
        LOG_WARN("PROCESS", "fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // Child: create a new process group so we can signal the whole group.
        setpgid(0, 0);
        execl(server_path, server_path, "--log-dir", log_dir, "--base-dir",
              base_dir, NULL);
        // execl only returns on error.
        _exit(127);
    }

    // Parent: pin the child into its own process group immediately to avoid
    // a race with the child's own setpgid call.
    setpgid(pid, pid);

    LOG_INFO("PROCESS", "Server spawned: PID %d", pid);
    return pid;
}

void process_wait_for_server(const char* host, int port, int max_wait_ms) {
    int elapsed = 0;

    while (elapsed < max_wait_ms) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            break;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port   = htons((uint16_t)port);
        inet_pton(AF_INET, host, &addr.sin_addr);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            close(fd);
            LOG_INFO("PROCESS", "Server ready after %dms", elapsed);
            return;
        }

        close(fd);
        usleep(50 * 1000);
        elapsed += 50;
    }

    LOG_WARN("PROCESS", "Server not ready after %dms, proceeding anyway",
             max_wait_ms);
}

int process_health_check(const char* host, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    /* Non-blocking connect so we can enforce timeout_ms on the connect itself
     */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    if (rc != 0) {
        /* Wait for connect to complete */
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = {
            .tv_sec  = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        int sel = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (sel <= 0) {
            close(fd);
            return -1; /* timeout or select error */
        }
        /* Check whether connect actually succeeded */
        int       err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            close(fd);
            return -1;
        }
    }

    /* Restore blocking mode and set send/recv timeouts for data phase */
    fcntl(fd, F_SETFL, flags);
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Send minimal HTTP/1.0 request (server closes conn after response) */
    char req[128];
    int  req_len = snprintf(req, sizeof(req),
                            "GET /health HTTP/1.0\r\nHost: %s\r\n\r\n", host);
    if (write(fd, req, (size_t)req_len) < 0) {
        close(fd);
        return -1;
    }

    /* Read the status line byte-by-byte until \n */
    char line[64];
    int  i = 0;
    while (i < (int)sizeof(line) - 1) {
        char    c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            break;
        }
        line[i++] = c;
    }
    line[i] = '\0';
    close(fd);

    int status_code = 0;
    if (sscanf(line, "HTTP/1.%*d %d", &status_code) != 1) {
        return -1;
    }

    return (status_code == 200) ? 0 : -1;
}

int process_monitor(pid_t pid) {
    int   status;
    pid_t r = waitpid(pid, &status, WNOHANG);

    if (r == 0) {
        // Still running
        return 0;
    }

    if (r < 0) {
        if (errno == ECHILD) {
            LOG_WARN("PROCESS", "Server PID %d not found (ECHILD)", pid);
        } else {
            LOG_WARN("PROCESS", "waitpid(%d): %s", pid, strerror(errno));
        }
        // Treat as crashed so the caller can decide whether to restart.
        return 1;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        LOG_INFO("PROCESS", "Server PID %d exited with code %d", pid, code);
        // Any exit from the server is treated as a crash. Only watchdog is the
        // one who knocks!
        return 1;
    }

    if (WIFSIGNALED(status)) {
        LOG_WARN("PROCESS", "Server PID %d killed by signal %d", pid,
                 WTERMSIG(status));
    }

    return 1;
}
