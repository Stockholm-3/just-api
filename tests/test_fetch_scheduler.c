/**
 * test_fetch_scheduler.c - Unit tests for FetchScheduler lifecycle
 *
 * All tests use NULL compute_pool so no network or file I/O is triggered.
 * With NULL pool, submit_fetch() is a no-op (early guard), keeping tests fast
 * and hermetic.
 *
 * Scheduling trigger logic (tm_min/tm_hour conditionals) is not covered here
 * because it requires either time injection or internal struct access — both
 * out of scope for this test suite.
 */

#include "energy_plan/compute.h"
#include "energy_plan/fetch_scheduler.h"

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

/* ============= Helpers ============= */

static FetchScheduler* g_sched = NULL;

static void teardown(void) {
    if (g_sched) {
        fetch_scheduler_destroy(g_sched);
        g_sched = NULL;
    }
}

static ComputeConfig make_cfg(void) {
    ComputeConfig cfg;
    compute_config_set_defaults(&cfg);
    return cfg;
}

/* ============= Tests ============= */

static int test_scheduler_create_null_pool(void) {
    ComputeConfig cfg = make_cfg();
    g_sched           = fetch_scheduler_create(NULL, &cfg);
    TEST_ASSERT(g_sched != NULL);
    return 0;
}

static int test_scheduler_destroy_null_pool(void) {
    ComputeConfig cfg = make_cfg();
    g_sched           = fetch_scheduler_create(NULL, &cfg);
    TEST_ASSERT(g_sched != NULL);
    fetch_scheduler_destroy(g_sched);
    g_sched = NULL; /* teardown must not double-free */
    return 0;
}

static int test_scheduler_callback_null_pool(void) {
    ComputeConfig cfg = make_cfg();
    g_sched           = fetch_scheduler_create(NULL, &cfg);
    TEST_ASSERT(g_sched != NULL);
    /* Single SMW tick — must not crash */
    fetch_scheduler_smw_callback(g_sched, 0);
    return 0;
}

static int test_scheduler_multiple_callbacks(void) {
    ComputeConfig cfg = make_cfg();
    g_sched           = fetch_scheduler_create(NULL, &cfg);
    TEST_ASSERT(g_sched != NULL);
    /* 10 consecutive ticks — no crash, no duplicate submissions (NULL pool is
     * a no-op anyway, but the guard logic still exercises the scheduling path)
     */
    for (int i = 0; i < 10; i++) {
        fetch_scheduler_smw_callback(g_sched, (uint64_t)i);
    }
    return 0;
}

static int test_scheduler_create_with_defaults(void) {
    /* Verify that compute_config_set_defaults() produces a cfg accepted by
     * fetch_scheduler_create() without errors */
    ComputeConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    compute_config_set_defaults(&cfg);

    g_sched = fetch_scheduler_create(NULL, &cfg);
    TEST_ASSERT(g_sched != NULL);
    return 0;
}

static int test_scheduler_create_zero_cfg(void) {
    /* A zeroed ComputeConfig (all empty paths) should still not crash —
     * the scheduler stores a copy and doesn't validate paths at creation */
    ComputeConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    g_sched = fetch_scheduler_create(NULL, &cfg);
    TEST_ASSERT(g_sched != NULL);
    return 0;
}

/* ============= main ============= */

int main(void) {
    printf("=== FetchScheduler Lifecycle Tests ===\n\n");

    RUN_TEST(test_scheduler_create_null_pool);
    RUN_TEST(test_scheduler_destroy_null_pool);
    RUN_TEST(test_scheduler_callback_null_pool);
    RUN_TEST(test_scheduler_multiple_callbacks);
    RUN_TEST(test_scheduler_create_with_defaults);
    RUN_TEST(test_scheduler_create_zero_cfg);

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        printf(", %d FAILED", g_tests_failed);
    }
    printf(" ===\n");

    return g_tests_failed > 0 ? 1 : 0;
}
