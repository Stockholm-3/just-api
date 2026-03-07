#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "process.h"

#include "logger/logger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
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
