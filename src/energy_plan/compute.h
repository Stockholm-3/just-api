#ifndef COMPUTE_H
#define COMPUTE_H

#include <stdbool.h> /* for bool in public API */

#define SLOTS_PER_DAY 96

typedef struct {
    char cities_csv[256];        /* e.g. "energy_plan/cities.csv" */
    char compute_input_dir[256]; /* e.g. "cache/compute_input" */
    char elpris_json[256]; /* e.g. "cache/compute_input/elpris_merged.json" */
    char output_dir[256];  /* e.g. "energy_plan/compute_output" */
    char lock_file[256];   /* e.g. "energy_plan/compute_output/.lock" */
} ComputeConfig;

typedef struct {
    double elpris[SLOTS_PER_DAY];
    double temperature[SLOTS_PER_DAY];
    double sun_intensity[SLOTS_PER_DAY];
    /* number of valid slots in the above arrays; may be less than
       SLOTS_PER_DAY when weather/prices data is incomplete.  A value of
       0 indicates that all slots in the arrays are valid. */
    int slots;
} AlgoInput;

typedef struct {
    double buy_electricity;
    double direct_use;
    double charge_battery;
    double sell_excess;
} AlgoOutput;

/**
 * Run the energy algorithm on the supplied input.
 *
 * @param input    filled input data; `input->slots` controls how many
 *                 entries of the arrays are considered (or 0 to use the
 *                 full SLOTS_PER_DAY).
 * @param output   populated with percentage statistics.
 * @param decisions  optional output array of per‑slot decisions; if non-NULL
 *                  it will be filled with true==buy/charge, false==sell/use.
 */
void run_energy_algorithm(const AlgoInput* input, AlgoOutput* output,
                          bool decisions[SLOTS_PER_DAY]);

/**
 * Populate cfg with the default paths used by the standalone compute binary.
 */
void compute_config_set_defaults(ComputeConfig* cfg);

/**
 * Run the full energy plan computation.
 *
 * Thread-safe: all state is stack-local or protected by the output lock.
 * The caller's logger must already be initialised.
 *
 * @return 0 on success, non-zero on error.
 */
int compute_run(const ComputeConfig* cfg);

#endif /* COMPUTE_H */