/**
 * @file energy_plan_inputs.h
 * @brief Parse energy-plan files into algo-ready input structs.
 *
 * This module is the translation layer between the filesystem (owned by
 * energy_plan_store) and the algorithm (defined in algo.h).
 *
 * It has no knowledge of the algorithm's logic, and no knowledge of
 * locks, output files, or the compute pipeline.
 *
 * Typical usage
 * -------------
 *   EpElprisData *elpris = ep_inputs_load_elpris();
 *   if (!elpris) { ... }
 *
 *   AlgoInput in;
 *   EpInputStatus s = ep_inputs_parse_city(city, zone, lat, lon, elpris, &in);
 *   if (s == EP_INPUT_OK) algo_run(&in, &out);
 *
 *   ep_inputs_free_elpris(elpris);
 */
#ifndef ENERGY_PLAN_INPUTS_H
#define ENERGY_PLAN_INPUTS_H

#include "algo.h" /* AlgoInput — parser fills this and  algo owns the shape */

#define EP_NUM_ZONES 4

/** Parsed price table for all four SE zones, one day. */
typedef struct {
    double sek_per_kwh[EP_NUM_ZONES][SLOTS_PER_DAY];
} EpElprisData;

/**
 * Load and parse the merged elpris JSON file.
 *
 * @return Heap-allocated EpElprisData on success, NULL on error.
 *         Caller must call ep_inputs_free_elpris().
 */
EpElprisData* ep_inputs_load_elpris(void);

void ep_inputs_free_elpris(EpElprisData* e);

typedef enum {
    EP_INPUT_OK,              /* in is fully populated and ready to use */
    EP_INPUT_NO_WEATHER,      /* weather file missing or malformed      */
    EP_INPUT_NO_ELPRIS,       /* elpris pointer was NULL                */
    EP_INPUT_PARTIAL_WEATHER, /* weather loaded but fewer than SLOTS_PER_DAY
                                 slots */
} EpInputStatus;

/**
 * Parse weather + prices for one city into an AlgoInput.
 *
 * EP_INPUT_PARTIAL_WEATHER is a warning, not a hard failure:
 *   @p in is populated and @p in->slots reflects the actual count.
 *   The caller decides whether to proceed.
 *
 * @param city       City name (used to locate the weather file).
 * @param zone       Price zone string, e.g. "SE3".
 * @param lat / lon  Coordinates (used to locate the weather file).
 * @param elpris     Pre-loaded price table.
 * @param[out] in    AlgoInput to fill. Undefined on error statuses.
 * @return EpInputStatus.
 */
EpInputStatus ep_inputs_parse_city(const char* city, const char* zone,
                                   double lat, double lon,
                                   const EpElprisData* elpris, AlgoInput* in);

/** Map "SE1".."SE4" to zone index 0..3. Unknown strings map to 2 (SE3). */
int ep_inputs_zone_index(const char* zone);

#endif // ENERGY_PLAN_INPUTS_H
