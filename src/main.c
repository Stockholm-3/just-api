#include "smw.h"
#include "utils.h"
#include "weather_server.h"

#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

static volatile sig_atomic_t g_shutdown_requested = 0;

static void handle_shutdown_signal(int signum) {
    (void)signum;
    g_shutdown_requested = 1;
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, handle_shutdown_signal);
    signal(SIGINT, handle_shutdown_signal);
    printf("[MAIN] Signal handlers configured\n");

    struct rlimit rlim;
    getrlimit(RLIMIT_NOFILE, &rlim);
    rlim.rlim_cur = 65536;
    setrlimit(RLIMIT_NOFILE, &rlim);
    printf("[MAIN] FD limit: %lu\n", rlim.rlim_cur);

    smw_init();

    WeatherServer server;
    weather_server_initiate(&server);

    printf("[MAIN] Server started on port 10680 (PID %d)\n", getpid());

    while (!g_shutdown_requested) {
        smw_work(system_monotonic_ms());
    }

    printf("[MAIN] Shutdown signal received, cleaning up...\n");
    weather_server_dispose(&server);
    smw_dispose();
    printf("[MAIN] Server stopped gracefully\n");

    return 0;
}