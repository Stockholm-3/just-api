/**
 * test_compute_config.c - Unit tests for ComputeConfig defaults and
 * worker-split formula
 */

#include "energy_plan/compute.h"

#include <stdio.h>
#include <string.h>

/* ============= Test Harness ============= */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                         \
            printf("  ASSERT FAILED: %s (line %d)\n", #expr, __LINE__);        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define TEST_ASSERT_EQ(a, b)                                                   \
    do {                                                                       \
        int _a = (a), _b = (b);                                                \
        if (_a != _b) {                                                        \
            printf("  ASSERT_EQ FAILED: %s == %d, expected %s == %d "          \
                   "(line %d)\n",                                              \
                   #a, _a, #b, _b, __LINE__);                                  \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define TEST_ASSERT_STR_EQ(a, b)                                               \
    do {                                                                       \
        if (strcmp((a), (b)) != 0) {                                           \
            printf("  ASSERT_STR_EQ FAILED: \"%s\" != \"%s\" (line %d)\n",     \
                   (a), (b), __LINE__);                                        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                       \
        g_tests_run++;                                                         \
        int _result = fn();                                                    \
        teardown();                                                            \
        if (_result == 0) {                                                    \
            g_tests_passed++;                                                  \
            printf("[PASS] %s\n", #fn);                                        \
        } else {                                                               \
            g_tests_failed++;                                                  \
            printf("[FAIL] %s\n", #fn);                                        \
        }                                                                      \
    } while (0)

static void teardown(void) {}

/* ============= Worker-split helper (mirrors server/main.c logic) =============
 */

static void worker_split(int total, int* req, int* comp) {
    *req  = (total + 1) / 2; /* ceil — request gets priority */
    *comp = total / 2;       /* floor */
}

/* ============= Tests: ComputeConfig defaults ============= */

static int test_compute_defaults_paths(void) {
    ComputeConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    compute_config_set_defaults(&cfg);

    TEST_ASSERT_STR_EQ(cfg.cities_csv, "energy_plan/cities.csv");
    TEST_ASSERT_STR_EQ(cfg.compute_input_dir, "cache/compute_input");
    TEST_ASSERT_STR_EQ(cfg.elpris_json,
                       "cache/compute_input/elpris_merged.json");
    TEST_ASSERT_STR_EQ(cfg.output_dir, "energy_plan/compute_output");
    TEST_ASSERT_STR_EQ(cfg.lock_file, "energy_plan/compute_output/.lock");

    return 0;
}

static int test_compute_defaults_null_safe(void) {
    /* Calling with a valid zeroed struct must not crash */
    ComputeConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    compute_config_set_defaults(&cfg);
    /* All fields must be non-empty after defaults */
    TEST_ASSERT(cfg.cities_csv[0] != '\0');
    TEST_ASSERT(cfg.compute_input_dir[0] != '\0');
    TEST_ASSERT(cfg.elpris_json[0] != '\0');
    TEST_ASSERT(cfg.output_dir[0] != '\0');
    TEST_ASSERT(cfg.lock_file[0] != '\0');
    return 0;
}

/* ============= Tests: Worker-split formula ============= */

static int test_worker_split_even(void) {
    int req, comp;

    worker_split(2, &req, &comp);
    TEST_ASSERT_EQ(req, 1);
    TEST_ASSERT_EQ(comp, 1);

    worker_split(4, &req, &comp);
    TEST_ASSERT_EQ(req, 2);
    TEST_ASSERT_EQ(comp, 2);

    worker_split(6, &req, &comp);
    TEST_ASSERT_EQ(req, 3);
    TEST_ASSERT_EQ(comp, 3);

    worker_split(8, &req, &comp);
    TEST_ASSERT_EQ(req, 4);
    TEST_ASSERT_EQ(comp, 4);

    return 0;
}

static int test_worker_split_odd(void) {
    int req, comp;

    worker_split(3, &req, &comp);
    TEST_ASSERT_EQ(req, 2);
    TEST_ASSERT_EQ(comp, 1);

    worker_split(5, &req, &comp);
    TEST_ASSERT_EQ(req, 3);
    TEST_ASSERT_EQ(comp, 2);

    worker_split(7, &req, &comp);
    TEST_ASSERT_EQ(req, 4);
    TEST_ASSERT_EQ(comp, 3);

    return 0;
}

static int test_worker_split_edge_one(void) {
    int req, comp;

    /* 1 total → request=1, compute=0 (no compute pool created) */
    worker_split(1, &req, &comp);
    TEST_ASSERT_EQ(req, 1);
    TEST_ASSERT_EQ(comp, 0);

    return 0;
}

static int test_worker_split_request_always_ge_compute(void) {
    /* For any positive count, request workers >= compute workers */
    for (int total = 1; total <= 32; total++) {
        int req, comp;
        worker_split(total, &req, &comp);
        TEST_ASSERT(req >= comp);
        TEST_ASSERT(req + comp == total);
        TEST_ASSERT(req >= 1);
    }
    return 0;
}

/* ============= main ============= */

int main(void) {
    printf("=== ComputeConfig + Worker-Split Tests ===\n\n");

    RUN_TEST(test_compute_defaults_paths);
    RUN_TEST(test_compute_defaults_null_safe);
    RUN_TEST(test_worker_split_even);
    RUN_TEST(test_worker_split_odd);
    RUN_TEST(test_worker_split_edge_one);
    RUN_TEST(test_worker_split_request_always_ge_compute);

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        printf(", %d FAILED", g_tests_failed);
    }
    printf(" ===\n");

    return g_tests_failed > 0 ? 1 : 0;
}
