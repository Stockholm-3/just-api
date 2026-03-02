/**
 * @file energy_parser_main.c
 * @brief Compute binary entry point.
 *
 * Initialises the energy-plan store, runs the compute pipeline via
 * energy_plan_store_run_compute(), and shuts down. All I/O, locking,
 * and file management is handled by the store.
 *
 * The only application-specific logic here is the decision algorithm.
 * Replace the body of algorithm() with the real implementation when ready.
 */

#include "energy_plan/energy_plan_store.h"
#include "logger/logger.h"

#include <jansson.h>
#include <stdbool.h>
#include <stdio.h>

#define CFG_ENERGY_PLAN_BASE_DIR "energy_plan"
#define CFG_LOG_DIR "logs/energy_parser"
#define CFG_MAX_CITIES 200
#define CFG_CITY_TTL_SECONDS (2UL * 24 * 3600)
#define CFG_SLOTS_PER_DAY 96

#define LOG_MOD "ENERGY"

static void algorithm(const char* city, const char* zone_str, json_t* weather,
                      int weather_count, json_t* prices, int slots_per_day,
                      bool* out_decisions) {
    (void)city;
    (void)zone_str;
    (void)weather;
    (void)weather_count;

    /* TODO: replace with a real algorithm. */
    double sum = 0.0;
    for (int i = 0; i < slots_per_day; i++) {
        json_t* slot = json_array_get(prices, (size_t)i);
        sum += json_number_value(json_object_get(slot, "SEK_per_kWh"));
    }
    double avg = sum / slots_per_day;

    for (int i = 0; i < slots_per_day; i++) {
        json_t* slot  = json_array_get(prices, (size_t)i);
        double  price = json_number_value(json_object_get(slot, "SEK_per_kWh"));
        out_decisions[i] = price < avg;
    }
}

int main(void) {
    if (logger_init(CFG_LOG_DIR, LOG_DEBUG) != 0) {
        fprintf(stderr, "FATAL: could not initialise logger\n");
        return 1;
    }
    LOG_INFO(LOG_MOD, "Energy parser starting");

    EpStoreConfig store_cfg = {
        .base_dir         = CFG_ENERGY_PLAN_BASE_DIR,
        .max_cities       = CFG_MAX_CITIES,
        .city_ttl_seconds = CFG_CITY_TTL_SECONDS,
    };
    if (energy_plan_store_init(&store_cfg) != 0) {
        LOG_WARN(LOG_MOD, "Failed to initialise energy plan store");
        logger_shutdown();
        return 1;
    }

    int result = energy_plan_store_run_compute(CFG_SLOTS_PER_DAY, algorithm);

    energy_plan_store_shutdown();
    logger_shutdown();

    return result < 0 ? 1 : result;
}
