#include "energy_plan/compute.h"
#include "logger.h"

#include <stdio.h>

#define LOG_DIR "logs/energy_parser"

int main(void) {
    if (logger_init(LOG_DIR, LOG_DEBUG) != 0) {
        fprintf(stderr, "FATAL: could not initialise logger\n");
        return 1;
    }

    ComputeConfig cfg;
    compute_config_set_defaults(&cfg);

    int result = compute_run(&cfg);

    logger_shutdown();
    return result;
}
