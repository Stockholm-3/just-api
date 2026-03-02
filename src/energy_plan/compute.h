#ifndef COMPUTE_H
#define COMPUTE_H

typedef struct {
    char cities_csv[256];        /* e.g. "energy_plan/cities.csv" */
    char compute_input_dir[256]; /* e.g. "cache/compute_input" */
    char elpris_json[256]; /* e.g. "cache/compute_input/elpris_merged.json" */
    char output_dir[256];  /* e.g. "energy_plan/compute_output" */
    char lock_file[256];   /* e.g. "energy_plan/compute_output/.lock" */
} ComputeConfig;

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
