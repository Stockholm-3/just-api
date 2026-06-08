/**
 * test_watchdog_config.c - Unit tests for watchdog config fields
 *
 * Tests the four new fields added to the watchdog config section:
 *   health_check_interval_ms, health_check_timeout_ms,
 *   health_check_failures, sigkill_timeout_ms
 */

#include "config/config_parser.h"
#include "config/config_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* ============= Helper ============= */

static const char* write_config(const char* json) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/test_watchdog_cfg_%d.json", getpid());
    FILE* f = fopen(path, "w");
    fputs(json, f);
    fclose(f);
    return path;
}

/* ============= Default value tests ============= */

static int test_defaults_health_check_interval(void) {
    ServerConfig cfg;
    config_set_defaults(&cfg);
    TEST_ASSERT_EQ(cfg.watchdog.health_check_interval_ms, 5000);
    return 0;
}

static int test_defaults_health_check_timeout(void) {
    ServerConfig cfg;
    config_set_defaults(&cfg);
    TEST_ASSERT_EQ(cfg.watchdog.health_check_timeout_ms, 2000);
    return 0;
}

static int test_defaults_health_check_failures(void) {
    ServerConfig cfg;
    config_set_defaults(&cfg);
    TEST_ASSERT_EQ(cfg.watchdog.health_check_failures, 3);
    return 0;
}

static int test_defaults_sigkill_timeout(void) {
    ServerConfig cfg;
    config_set_defaults(&cfg);
    TEST_ASSERT_EQ(cfg.watchdog.sigkill_timeout_ms, 5000);
    return 0;
}

/* ============= JSON parsing tests ============= */

static int test_parse_health_check_interval(void) {
    const char* path =
        write_config("{\"watchdog\":{\"health_check_interval_ms\":1000}}");
    ServerConfig cfg;
    int          rc = config_parser_load(path, &cfg);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(cfg.watchdog.health_check_interval_ms, 1000);
    return 0;
}

static int test_parse_all_new_fields(void) {
    const char*  path = write_config("{"
                                      "\"watchdog\":{"
                                      "\"health_check_interval_ms\":1000,"
                                      "\"health_check_timeout_ms\":500,"
                                      "\"health_check_failures\":5,"
                                      "\"sigkill_timeout_ms\":10000"
                                      "}"
                                      "}");
    ServerConfig cfg;
    int          rc = config_parser_load(path, &cfg);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(cfg.watchdog.health_check_interval_ms, 1000);
    TEST_ASSERT_EQ(cfg.watchdog.health_check_timeout_ms, 500);
    TEST_ASSERT_EQ(cfg.watchdog.health_check_failures, 5);
    TEST_ASSERT_EQ(cfg.watchdog.sigkill_timeout_ms, 10000);
    return 0;
}

static int test_parse_disable_health_check(void) {
    const char* path =
        write_config("{\"watchdog\":{\"health_check_interval_ms\":0}}");
    ServerConfig cfg;
    int          rc = config_parser_load(path, &cfg);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(cfg.watchdog.health_check_interval_ms, 0);
    return 0;
}

/* ============= main ============= */

int main(void) {
    printf("=== Watchdog Config Tests ===\n\n");

    RUN_TEST(test_defaults_health_check_interval);
    RUN_TEST(test_defaults_health_check_timeout);
    RUN_TEST(test_defaults_health_check_failures);
    RUN_TEST(test_defaults_sigkill_timeout);
    RUN_TEST(test_parse_health_check_interval);
    RUN_TEST(test_parse_all_new_fields);
    RUN_TEST(test_parse_disable_health_check);

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return g_tests_failed > 0 ? 1 : 0;
}
