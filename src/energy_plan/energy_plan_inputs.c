#include "energy_plan_inputs.h"

#include "energy_plan_store.h"
#include "logger/logger.h"

#include <jansson.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MOD "EP_INPUTS"

int ep_inputs_zone_index(const char* zone) {
    if (strstr(zone, "SE1")) {
        return 0;
    }
    if (strstr(zone, "SE2")) {
        return 1;
    }
    if (strstr(zone, "SE3")) {
        return 2;
    }
    if (strstr(zone, "SE4")) {
        return 3;
    }
    LOG_WARN(LOG_MOD, "unknown zone '%s', defaulting to SE3", zone);
    return 2;
}

EpElprisData* ep_inputs_load_elpris(void) {
    char path[512];
    if (energy_plan_store_get_elpris_path(path, sizeof(path)) != 0) {
        LOG_WARN(LOG_MOD, "get_elpris_path failed");
        return NULL;
    }

    json_error_t err;
    json_t*      root = json_load_file(path, 0, &err);
    if (!root || !json_is_array(root)) {
        LOG_WARN(LOG_MOD, "cannot load elpris '%s': %s", path, err.text);
        json_decref(root);
        return NULL;
    }

    EpElprisData* e = calloc(1, sizeof(*e));
    if (!e) {
        json_decref(root);
        return NULL;
    }

    size_t total    = json_array_size(root);
    int    expected = EP_NUM_ZONES * SLOTS_PER_DAY;

    if ((int)total < expected) {
        LOG_WARN(LOG_MOD, "elpris: expected %d entries, got %zu", expected,
                 total);
    }

    for (int i = 0; i < expected && i < (int)total; i++) {
        json_t* obj              = json_array_get(root, i);
        int     zone             = i / SLOTS_PER_DAY;
        int     sl               = i % SLOTS_PER_DAY;
        json_t* sek              = json_object_get(obj, "SEK_per_kWh");
        e->sek_per_kwh[zone][sl] = sek ? json_number_value(sek) : 0.0;
    }

    json_decref(root);
    LOG_INFO(LOG_MOD, "Loaded %zu elpris entries", total);
    return e;
}

void ep_inputs_free_elpris(EpElprisData* e) { free(e); }

EpInputStatus ep_inputs_parse_city(const char* city, const char* zone,
                                   double lat, double lon,
                                   const EpElprisData* elpris, AlgoInput* in) {
    memset(in, 0, sizeof(*in));

    if (!elpris) {
        return EP_INPUT_NO_ELPRIS;
    }

    int zi = ep_inputs_zone_index(zone);
    memcpy(in->elpris, elpris->sek_per_kwh[zi], sizeof(in->elpris));

    char path[512];
    if (energy_plan_store_get_weather_path(city, lat, lon, path,
                                           sizeof(path)) != 0) {
        LOG_WARN(LOG_MOD, "'%s': weather path overflow", city);
        return EP_INPUT_NO_WEATHER;
    }

    json_error_t err;
    json_t*      root = json_load_file(path, 0, &err);
    if (!root) {
        LOG_WARN(LOG_MOD, "'%s': cannot load weather: %s", city, err.text);
        return EP_INPUT_NO_WEATHER;
    }

    json_t* forecast =
        json_object_get(json_object_get(root, "data"), "minutely_forecast");
    if (!json_is_array(forecast)) {
        LOG_WARN(LOG_MOD, "'%s': no minutely_forecast array", city);
        json_decref(root);
        return EP_INPUT_NO_WEATHER;
    }

    int     count = 0;
    size_t  idx;
    json_t* slot;
    json_array_foreach(forecast, idx, slot) {
        if (count >= SLOTS_PER_DAY) {
            break;
        }

        json_t* temp = json_object_get(slot, "temperature");
        json_t* sun  = json_object_get(slot, "sun_intensity");

        in->temperature[count]   = temp ? json_number_value(temp) : 0.0;
        in->sun_intensity[count] = sun ? json_number_value(sun) : 0.0;
        count++;
    }
    json_decref(root);

    in->slots = (count < SLOTS_PER_DAY) ? count : 0;

    if (count == 0) {
        LOG_WARN(LOG_MOD, "'%s': forecast array was empty", city);
        return EP_INPUT_NO_WEATHER;
    }
    if (count < SLOTS_PER_DAY) {
        LOG_WARN(LOG_MOD, "'%s': partial weather %d/%d slots", city, count,
                 SLOTS_PER_DAY);
        return EP_INPUT_PARTIAL_WEATHER;
    }
    return EP_INPUT_OK;
}
