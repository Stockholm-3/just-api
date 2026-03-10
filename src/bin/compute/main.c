/**
 * @file compute_main.c
 */
#include "algo.h"
#include "config/config_parser.h"
#include "energy_plan/energy_plan_inputs.h"
#include "energy_plan/energy_plan_store.h"
#include "logger/logger.h"

#include <jansson.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MOD "COMPUTE"

// Replace gmtime_r with a Swedish-local conversion
static void format_swedish_time(time_t t, char* buf, size_t len) {
    struct tm utc;
    gmtime_r(&t, &utc);

    // Determine offset (same DST logic as elpris_api.c)
    int       year  = utc.tm_year + 1900;
    struct tm march = {
        .tm_year = year - 1900, .tm_mon = 2, .tm_mday = 31, .tm_hour = 1};
    mktime(&march);
    march.tm_mday -= march.tm_wday;
    struct tm october = {
        .tm_year = year - 1900, .tm_mon = 9, .tm_mday = 31, .tm_hour = 1};
    mktime(&october);
    october.tm_mday -= october.tm_wday;

    time_t dst_start = mktime(&march);
    time_t dst_end   = mktime(&october);
    int    offset_h  = (t >= dst_start && t < dst_end) ? 2 : 1;

    time_t    local = t + offset_h * 3600;
    struct tm tm_local;
    gmtime_r(&local, &tm_local);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm_local);
    // Append offset manually
    snprintf(buf + strlen(buf), len - strlen(buf), "+%02d:00", offset_h);
}

static json_t* slot_to_json(int idx, const AlgoInput* in, const AlgoSlot* s) {
    json_t* o = json_object();

    // Input variables
    json_t* input = json_object();
    json_object_set_new(input, "elpris", json_real(in->elpris[idx]));
    json_object_set_new(input, "temperature", json_real(in->temperature[idx]));
    json_object_set_new(input, "sun_intensity",
                        json_real(in->sun_intensity[idx]));
    json_object_set_new(o, "input_variables", input);

    // Algorithm output
    json_t* output = json_object();
    json_object_set_new(output, "buy_electricity",
                        json_real(s->buy_electricity));
    json_object_set_new(output, "direct_use", json_real(s->direct_use));
    json_object_set_new(output, "charge_battery", json_real(s->charge_battery));
    json_object_set_new(output, "sell_excess", json_real(s->sell_excess));
    json_object_set_new(o, "output", output);

    return o;
}

static json_t* summary_to_json(const AlgoSlot* s) {
    json_t* o = json_object();
    json_object_set_new(o, "buy_electricity", json_real(s->buy_electricity));
    json_object_set_new(o, "direct_use", json_real(s->direct_use));
    json_object_set_new(o, "charge_battery", json_real(s->charge_battery));
    json_object_set_new(o, "sell_excess", json_real(s->sell_excess));
    return o;
}

static json_t* build_output(const char* city, const char* zone, double lat,
                            double lon, time_t start_time, const AlgoInput* in,
                            const AlgoOutput* out) {
    int slots = (in->slots > 0 && in->slots <= SLOTS_PER_DAY) ? in->slots
                                                              : SLOTS_PER_DAY;

    // Format start_time as ISO-8601 UTC string, e.g. "2026-03-10T00:00:00Z"
    char      ts_buf[32] = "";
    struct tm tm_utc;
    if (start_time != 0) {
        format_swedish_time(start_time, ts_buf, sizeof(ts_buf));
    }

    json_t* root = json_object();
    json_object_set_new(root, "city", json_string(city));
    json_object_set_new(root, "price_zone", json_string(zone));
    json_object_set_new(root, "latitude", json_real(lat));
    json_object_set_new(root, "longitude", json_real(lon));
    json_object_set_new(root, "start_time", json_string(ts_buf));
    json_object_set_new(root, "slots_total", json_integer(slots));
    json_object_set_new(root, "summary", summary_to_json(&out->summary));

    json_t* decisions = json_array();
    for (int i = 0; i < slots; i++) {
        json_array_append_new(decisions, slot_to_json(i, in, &out->slots[i]));
    }
    json_object_set_new(root, "decisions", decisions);

    return root;
}

int main(void) {
    ServerConfig cfg;
    if (config_parser_load("config.json", &cfg) != 0) {
        config_set_defaults(&cfg);
    }

    if (logger_init(cfg.compute.log_dir, LOG_DEBUG) != 0) {
        return 1;
    }

    LOG_INFO(LOG_MOD, "Compute starting");

    EpStoreConfig store_cfg = {
        .base_dir   = cfg.energy_plan.base_dir,
        .max_cities = cfg.energy_plan.max_cities,
    };
    if (energy_plan_store_init(&store_cfg) != 0) {
        LOG_WARN(LOG_MOD, "Store init failed");
        logger_shutdown();
        return 1;
    }

    int ret  = 0;
    int lock = energy_plan_store_acquire_write_lock();
    if (lock < 0) {
        LOG_WARN(LOG_MOD, "Could not acquire write lock");
        ret = 1;
        goto shutdown;
    }

    EpCityList cities = energy_plan_store_load_cities();
    if (!cities.entries || cities.count == 0) {
        LOG_WARN(LOG_MOD, "No cities in registry");
        ret = 1;
        goto release;
    }

    EpElprisData* elpris = ep_inputs_load_elpris();
    if (!elpris) {
        LOG_WARN(LOG_MOD, "Failed to load elpris");
        free(cities.entries);
        ret = 1;
        goto release;
    }

    energy_plan_store_clear_outputs();

    int ok = 0, fail = 0;
    for (int i = 0; i < cities.count; i++) {
        EpCityEntry* e         = &cities.entries[i];
        const char*  city_name = e->key;
        const char*  zone      = e->tag;
        double       lat       = e->f1;
        double       lon       = e->f2;

        AlgoInput     in;
        EpInputStatus status =
            ep_inputs_parse_city(city_name, zone, lat, lon, elpris, &in);
        if (status == EP_INPUT_NO_WEATHER || status == EP_INPUT_NO_ELPRIS) {
            LOG_WARN(LOG_MOD, "'%s': skipping — inputs unavailable (%d)",
                     city_name, status);
            fail++;
            continue;
        }

        AlgoOutput out;
        algo_run(&in, &out);

        json_t* result = build_output(city_name, zone, lat, lon,
                                      elpris->start_time, &in, &out);
        int     rc = energy_plan_store_write_output(city_name, zone, result);
        json_decref(result);

        if (rc != 0) {
            LOG_WARN(LOG_MOD, "'%s': write failed", city_name);
            fail++;
        } else {
            LOG_INFO(LOG_MOD, "'%s' (%s): done", city_name, zone);
            ok++;
        }
    }

    ep_inputs_free_elpris(elpris);
    free(cities.entries);

    LOG_INFO(LOG_MOD, "Done: %d ok, %d failed", ok, fail);
    if (fail > 0) {
        ret = 1;
    }

release:
    energy_plan_store_release_write_lock(lock);
shutdown:
    energy_plan_store_shutdown();
    logger_shutdown();
    return ret;
}
