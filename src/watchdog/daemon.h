#ifndef DAEMON_H
#define DAEMON_H

// Forks twice, detaches from terminal, redirects stdio to /dev/null.
// Returns 0 on success, -1 on error.
#include <signal.h>
int daemon_daemonize(void);

// Writes the current PID to `path`. Returns 0 on success, -1 on error.
int daemon_write_pid_file(const char* path);

// Removes the PID file at `path`.
void daemon_remove_pid_file(const char* path);

// Installs SIGTERM / SIGINT handlers that set *shutdown_flag = 1 and
// optionally forward SIGTERM to the process group `server_pgid` (pass 0 to
// skip forwarding).  SIGCHLD is left at SIG_DFL.
void daemon_setup_signals(volatile sig_atomic_t* shutdown_flag,
                          pid_t*                 server_pid_ptr);

#endif // DAEMON_H
