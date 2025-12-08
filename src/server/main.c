#include "smw.h"
#include "utils.h"
#include "weather_server.h"

#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>

int main() {

    signal(SIGPIPE, SIG_IGN);
    printf("[MAIN] SIGPIPE handler set\n");

    struct rlimit rlim;
    getrlimit(RLIMIT_NOFILE, &rlim);
    rlim.rlim_cur = 65536;
    setrlimit(RLIMIT_NOFILE, &rlim);
    printf("[MAIN] FD limit: %lu\n", rlim.rlim_cur);
    smw_init();

    WeatherServer server;
    weather_server_initiate(&server);

    while (1) {
        smw_work(system_monotonic_ms());
    }

    weather_server_dispose(&server);

    smw_dispose();

    return 0;
}