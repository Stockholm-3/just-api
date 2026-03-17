#include "config/config_parser.h"
#include "energy_plan/energy_plan_store.h"
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
#include <sys/epoll.h>
#include <sys/resource.h>
#include <unistd.h>

#define DEFAULT_LOG_DIR "./logs"
#define DEFAULT_BASE_DIR "."
#define EPOLL_TIMEOUT_MS 10
#define EPOLL_MAX_EVENTS 2
static volatile sig_atomic_t g_shutdown_requested = 0;
static void                  handle_shutdown_signal(int signum) {
    (void)signum;
    g_shutdown_requested = 1;
}
int main(int argc, char* argv[]) {
    const char*          log_dir        = DEFAULT_LOG_DIR;
    const char*          base_dir       = DEFAULT_BASE_DIR;
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
    if (chdir(base_dir) != 0) {
        fprintf(stderr, "Failed to chdir to base directory: %s\n", base_dir);
        return 1;
    }
    if (logger_init(log_dir, LOG_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, handle_shutdown_signal);
    signal(SIGINT, handle_shutdown_signal);
    LOG_INFO("MAIN", "Signal handlers configured");
    ServerConfig config;
    const char*  config_file = "config.json";
    if (config_parser_load(config_file, &config) != 0) {
        printf("[MAIN] Using default configuration\n");
        config_set_defaults(&config);
    }
    if (config_parser_validate(&config) != 0) {
        fprintf(stderr, "[MAIN] Invalid configuration\n");
        logger_shutdown();
        return 1;
    }
    config_parser_print(&config);
    struct rlimit rlim;
    getrlimit(RLIMIT_NOFILE, &rlim);
    rlim.rlim_cur = config.max_connections;
    setrlimit(RLIMIT_NOFILE, &rlim);
    LOG_INFO("MAIN", "FD limit: %lu", rlim.rlim_cur);
    smw_init();
    // init energy_plan_store
    EpStoreConfig store_cfg = {
        .base_dir   = config.energy_plan.base_dir,
        .max_cities = config.energy_plan.max_cities,
    };
    if (energy_plan_store_init(&store_cfg) != 0) {
        LOG_ERROR("MAIN", "Failed to initialise energy plan store");
        smw_dispose();
        logger_shutdown();
        return 1;
    }
    ThreadPool* pool = thread_pool_create(config.thread_pool.num_workers,
                                          config.thread_pool.max_pending);
    if (!pool) {
        LOG_ERROR("MAIN", "Failed to create thread pool");
        energy_plan_store_shutdown();
        smw_dispose();
        logger_shutdown();
        return 1;
    }
    LOG_INFO("MAIN", "Thread pool created (%d workers, max_pending=%d)",
             config.thread_pool.num_workers, config.thread_pool.max_pending);
    /* thread_pool completions are driven by the self-pipe registered with
     * epoll below — no SMW polling task needed. */
    WeatherServer server;
    weather_server_initiate(&server, pool);
    LOG_INFO("MAIN", "Server started on port 10680 (PID %d)", getpid());

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        LOG_ERROR("MAIN", "epoll_create1 failed");
        weather_server_dispose(&server);
        thread_pool_destroy(pool);
        energy_plan_store_shutdown();
        smw_dispose();
        logger_shutdown();
        return 1;
    }

    struct epoll_event ev = {0};
    ev.events             = EPOLLIN;
    ev.data.fd            = server.httpServer.tcpServer.listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server.httpServer.tcpServer.listen_fd, &ev);

    int                notify_fd = thread_pool_get_notify_fd(pool);
    struct epoll_event notify_ev = {0};
    notify_ev.events             = EPOLLIN | EPOLLET;
    notify_ev.data.fd            = notify_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, notify_fd, &notify_ev);

    struct epoll_event events[EPOLL_MAX_EVENTS];
    while (!g_shutdown_requested) {
        int nfds = epoll_wait(epfd, events, EPOLL_MAX_EVENTS, EPOLL_TIMEOUT_MS);
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == notify_fd) {
                /* Drain the pipe, then dispatch all pending done callbacks */
                char buf[64];
                while (read(notify_fd, buf, sizeof(buf)) > 0) {
                }
                thread_pool_process_completions(pool);
            }
        }
        smw_work(system_monotonic_ms());
    }

    close(epfd);
    LOG_INFO("MAIN", "Shutdown signal received, cleaning up...");
    weather_server_dispose(&server);
    thread_pool_wait_idle(pool);
    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);
    LOG_INFO("MAIN", "Thread pool destroyed");
    energy_plan_store_shutdown();
    smw_dispose();
    logger_shutdown();
    return 0;
}
