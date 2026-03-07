#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>

// Forks and execs `server_path` with --log-dir and --base-dir arguments.
// The child is placed in its own process group.
// Returns the child PID on success, -1 on error.
pid_t process_spawn_server(const char* server_path, const char* log_dir,
                           const char* base_dir);

// Polls `host`:`port` every 50 ms until the server accepts a TCP connection
// or `max_wait_ms` elapses.
void process_wait_for_server(const char* host, int port, int max_wait_ms);

// Non-blocking check on `pid`.
// Returns  0 if the process is still running.
// Returns  1 if it exited with a non-zero status or was killed by a signal.
// Returns -1 if it exited cleanly (status 0) or is no longer a child.
int process_monitor(pid_t pid);

#endif // PROCESS_H
