#include "config/config_parser.h"
#include "logger/logger.h"
#include "smw.h"
#include "thread_pool.h"
#include "utils.h"
#include "weather_server.h"

#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#define DEFAULT_LOG_DIR "./logs"
#define DEFAULT_BASE_DIR "."

static volatile sig_atomic_t g_shutdown_requested = 0;

static void handle_shutdown_signal(int signum) {
    (void)signum;
    g_shutdown_requested = 1;
}

int main(int argc, char* argv[]) {
    const char* log_dir  = DEFAULT_LOG_DIR;
    const char* base_dir = DEFAULT_BASE_DIR;

    // Parse arguments
    static struct option long_options[] = {
        {"log-dir", required_argument, 0, 'l'},
        {"base-dir", required_argument, 0, 'b'},
        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "l:b:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'l':
            log_dir = optarg;
            break;
        case 'b':
            base_dir = optarg;
            break;
        default:
            break;
        }
    }

    // Change to base directory so relative paths (cache, data) resolve
    // correctly
    if (chdir(base_dir) != 0) {
        fprintf(stderr, "Failed to chdir to base directory: %s\n", base_dir);
        return 1;
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

    /* Load configuration */
    ServerConfig config;
    const char*  config_file = "config.json";

    if (config_parser_load(config_file, &config) != 0) {
        printf("[MAIN] Using default configuration\n");
        config_set_defaults(&config);
    }

    if (config_parser_validate(&config) != 0) {
        fprintf(stderr, "[MAIN] Invalid configuration\n");
        return 1;
    }

    config_parser_print(&config);

    struct rlimit rlim;
    getrlimit(RLIMIT_NOFILE, &rlim);
    rlim.rlim_cur = config.max_connections;
    setrlimit(RLIMIT_NOFILE, &rlim);
    LOG_INFO("MAIN", "FD limit: %lu", rlim.rlim_cur);

    smw_init();

    /* Initialize thread pool */
    ThreadPool* pool = thread_pool_create(config.thread_pool.num_workers,
                                          config.thread_pool.max_pending);
    if (!pool) {
        LOG_ERROR("MAIN", "Failed to create thread pool");
        smw_dispose();
        logger_shutdown();
        return 1;
    }
    LOG_INFO("MAIN", "Thread pool created (%d workers, max_pending=%d)",
             config.thread_pool.num_workers, config.thread_pool.max_pending);

    /* SMW task: drain completion queue every event-loop cycle */
    smw_create_task(pool, thread_pool_smw_callback);

    WeatherServer server;
    weather_server_initiate(&server);

    LOG_INFO("MAIN", "Server started on port 10680 (PID %d)", getpid());

    while (!g_shutdown_requested) {
        smw_work(system_monotonic_ms());
    }

    LOG_INFO("MAIN", "Shutdown signal received, cleaning up...");
    weather_server_dispose(&server);
    thread_pool_wait_idle(pool);
    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);
    LOG_INFO("MAIN", "Thread pool destroyed");
    smw_dispose();

    logger_shutdown();
    return 0;
}
