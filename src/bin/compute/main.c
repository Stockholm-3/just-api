/**
 * @file compute_main.c
 */
#include "algo.h"
#include "energy_plan/energy_plan_inputs.h"
#include "energy_plan/energy_plan_store.h"
#include "logger/logger.h"

#include <jansson.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MOD "COMPUTE"
#define CFG_BASE_DIR "energy_plan"
#define CFG_LOG_DIR "logs/energy_parser"
#define CFG_MAX_CITIES 200

static json_t* slot_to_json(int idx, const AlgoInput* in, const AlgoSlot* s) {
    json_t* o = json_object();
    json_object_set_new(o, "slot", json_integer(idx));
    json_object_set_new(o, "elpris", json_real(in->elpris[idx]));
    json_object_set_new(o, "temperature", json_real(in->temperature[idx]));
    json_object_set_new(o, "sun_intensity", json_real(in->sun_intensity[idx]));
    json_object_set_new(o, "buy_electricity", json_real(s->buy_electricity));
    json_object_set_new(o, "direct_use", json_real(s->direct_use));
    json_object_set_new(o, "charge_battery", json_real(s->charge_battery));
    json_object_set_new(o, "sell_excess", json_real(s->sell_excess));
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
                            double lon, const AlgoInput* in,
                            const AlgoOutput* out) {
    int slots = (in->slots > 0 && in->slots <= SLOTS_PER_DAY) ? in->slots
                                                              : SLOTS_PER_DAY;

    json_t* root = json_object();
    json_object_set_new(root, "city", json_string(city));
    json_object_set_new(root, "price_zone", json_string(zone));
    json_object_set_new(root, "latitude", json_real(lat));
    json_object_set_new(root, "longitude", json_real(lon));
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
    if (logger_init(CFG_LOG_DIR, LOG_DEBUG) != 0) {
        return 1;
    }

    LOG_INFO(LOG_MOD, "Compute starting");

    EpStoreConfig cfg = {
        .base_dir   = CFG_BASE_DIR,
        .max_cities = CFG_MAX_CITIES,
    };
    if (energy_plan_store_init(&cfg) != 0) {
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

        json_t* result = build_output(city_name, zone, lat, lon, &in, &out);
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
