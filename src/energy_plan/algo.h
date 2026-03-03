#ifndef ALGO_H
#define ALGO_H

#define SLOTS_PER_DAY 96

typedef struct {
    double elpris[SLOTS_PER_DAY];
    double temperature[SLOTS_PER_DAY];
    double sun_intensity[SLOTS_PER_DAY];

    /**
     * Number of valid slots. 0 means all SLOTS_PER_DAY are valid.
     * Set when weather/price data is incomplete.
     */
    int slots;
} AlgoInput;

/**
 * Per-slot recommendation as fractions in [0, 1].
 * The four values sum to 1.0 for every valid slot.
 */
typedef struct {
    double buy_electricity;
    double direct_use;
    double charge_battery;
    double sell_excess;
} AlgoSlot;

/**
 * Full output for one city/day.
 *
 * slots[i]  – per-slot fractions, valid for i in [0, effective_slots).
 * summary   – mean of each fraction across all effective slots.
 */
typedef struct {
    AlgoSlot slots[SLOTS_PER_DAY];
    AlgoSlot summary;
} AlgoOutput;

/**
 * Run the energy algorithm for one city/day.
 * Fills all fields of @p output. Never returns partial results.
 */
void algo_run(const AlgoInput* input, AlgoOutput* output);

#endif // ALGO_H
