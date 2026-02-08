#include "logger/logger.h"
#include "smw.h"
#include "utils.h"
#include "weather_server.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#define DEFAULT_LOG_DIR "./logs"

static volatile sig_atomic_t g_shutdown_requested = 0;

static void handle_shutdown_signal(int signum) {
    (void)signum;
    g_shutdown_requested = 1;
}

int main(int argc, char* argv[]) {
    const char* log_dir = DEFAULT_LOG_DIR;

    // Parse --log-dir argument
    static struct option long_options[] = {
        {"log-dir", required_argument, 0, 'l'}, {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "l:", long_options, NULL)) != -1) {
        if (opt == 'l') {
            log_dir = optarg;
        }
    }

    // Initialize logger
    if (logger_init(log_dir, LOG_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, handle_shutdown_signal);
    signal(SIGINT, handle_shutdown_signal);
    LOG_INFO("MAIN", "Signal handlers configured");

    struct rlimit rlim;
    getrlimit(RLIMIT_NOFILE, &rlim);
    rlim.rlim_cur = 65536;
    setrlimit(RLIMIT_NOFILE, &rlim);
    LOG_INFO("MAIN", "FD limit: %lu", rlim.rlim_cur);

    smw_init();

    WeatherServer server;
    weather_server_initiate(&server);

    LOG_INFO("MAIN", "Server started on port 10680 (PID %d)", getpid());

    while (!g_shutdown_requested) {
        smw_work(system_monotonic_ms());
    }

    LOG_INFO("MAIN", "Shutdown signal received, cleaning up...");
    weather_server_dispose(&server);
    smw_dispose();
    LOG_INFO("MAIN", "Server stopped gracefully");

    logger_shutdown();
    return 0;
}
