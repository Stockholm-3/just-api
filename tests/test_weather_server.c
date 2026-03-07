/**
 * test_weather_server.c - Unit tests for weather_server module
 *
 * Tests every function in weather_server.c:
 *   - weather_server_initiate
 *   - weather_server_initiate_ptr
 *   - weather_server_on_http_connection  (internal, declared extern)
 *   - weather_server_task_work           (internal, declared extern)
 *   - weather_server_dispose
 *   - weather_server_dispose_ptr
 *
 * Each test that requires a live WeatherServer calls setup_server() at the
 * start; the RUN_TEST macro calls teardown() after every test function
 * returns so the server is disposed before the next test creates one.
 */

#include "http_server_connection.h"
#include "smw.h"
#include "weather_server.h"

#include <stdio.h>
#include <stdlib.h>
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

/* ============= Internal function declarations ============= */

/**
 * weather_server_on_http_connection and weather_server_task_work are defined
 * without static in weather_server.c, so they are accessible via extern.
 */
extern int  weather_server_on_http_connection(void*                 context,
                                              HTTPServerConnection* connection);
extern void weather_server_task_work(void* context, uint64_t mon_time);

/* ============= Helpers ============= */

static WeatherServer* g_server = NULL;

static void setup_server(void) { weather_server_initiate_ptr(&g_server, NULL); }

/**
 * Dispose the server if one was created during the test.
 * Called by RUN_TEST after every test function returns.
 */
static void teardown(void) {
    if (g_server != NULL) {
        weather_server_dispose_ptr(&g_server);
    }
}

/* ============= weather_server_initiate_ptr ============= */

static int test_initiate_ptr_null_server_ptr_returns_minus1(void) {
    /* server_ptr is NULL → -1 without any allocation */
    int result = weather_server_initiate_ptr(NULL, NULL);
    TEST_ASSERT_EQ(result, -1);
    return 0;
}

static int test_initiate_ptr_success(void) {
    int result = weather_server_initiate_ptr(&g_server, NULL);

    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT(g_server != NULL);
    return 0;
}

static int test_initiate_ptr_initialises_instances_list(void) {
    weather_server_initiate_ptr(&g_server, NULL);

    TEST_ASSERT(g_server->instances != NULL);
    return 0;
}

static int test_initiate_ptr_initialises_epoll_fd(void) {
    weather_server_initiate_ptr(&g_server, NULL);

    /* epoll_create1 returns a valid (>= 0) fd on success */
    TEST_ASSERT(g_server->conn_epfd >= 0);
    return 0;
}

static int test_initiate_ptr_initialises_task(void) {
    weather_server_initiate_ptr(&g_server, NULL);

    TEST_ASSERT(g_server->task != NULL);
    return 0;
}

static int test_initiate_ptr_zeroes_timeout_scan(void) {
    weather_server_initiate_ptr(&g_server, NULL);

    TEST_ASSERT(g_server->last_timeout_scan_ms == 0);
    return 0;
}

/* ============= weather_server_initiate (stack-allocated) ============= */

static int test_initiate_stack_returns_zero(void) {
    WeatherServer server;
    int           result = weather_server_initiate(&server, NULL);

    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT(server.instances != NULL);
    TEST_ASSERT(server.conn_epfd >= 0);
    TEST_ASSERT(server.task != NULL);

    weather_server_dispose(&server);
    return 0;
}

/* ============= weather_server_dispose_ptr ============= */

static int test_dispose_ptr_null_no_crash(void) {
    weather_server_dispose_ptr(NULL);
    return 0;
}

static int test_dispose_ptr_null_server_no_crash(void) {
    WeatherServer* server = NULL;
    weather_server_dispose_ptr(&server);
    return 0;
}

static int test_dispose_ptr_sets_pointer_to_null(void) {
    weather_server_initiate_ptr(&g_server, NULL);
    TEST_ASSERT(g_server != NULL);

    weather_server_dispose_ptr(&g_server);
    TEST_ASSERT(g_server == NULL);
    return 0;
}

/* ============= weather_server_on_http_connection ============= */

static int test_on_http_connection_returns_zero(void) {
    setup_server();

    HTTPServerConnection conn;
    memset(&conn, 0, sizeof(conn));

    int result = weather_server_on_http_connection(g_server, &conn);

    TEST_ASSERT_EQ(result, 0);
    return 0;
}

static int test_on_http_connection_adds_instance_to_list(void) {
    setup_server();

    HTTPServerConnection conn;
    memset(&conn, 0, sizeof(conn));

    size_t size_before = g_server->instances->size;
    weather_server_on_http_connection(g_server, &conn);
    size_t size_after = g_server->instances->size;

    TEST_ASSERT(size_after == size_before + 1);
    return 0;
}

static int test_on_http_connection_multiple_connections(void) {
    setup_server();

    HTTPServerConnection conn1, conn2, conn3;
    memset(&conn1, 0, sizeof(conn1));
    memset(&conn2, 0, sizeof(conn2));
    memset(&conn3, 0, sizeof(conn3));

    weather_server_on_http_connection(g_server, &conn1);
    weather_server_on_http_connection(g_server, &conn2);
    weather_server_on_http_connection(g_server, &conn3);

    TEST_ASSERT(g_server->instances->size == 3);
    return 0;
}

/* ============= weather_server_task_work ============= */

static int test_task_work_no_crash_empty_list_no_scan(void) {
    /* mon_time=0: 0 - 0 < 1000, so timeout scan is skipped */
    setup_server();
    weather_server_task_work(g_server, 0);
    return 0;
}

static int test_task_work_no_crash_empty_list_with_scan(void) {
    /* mon_time=2000: 2000 - 0 >= 1000, timeout scan runs over empty list */
    setup_server();
    weather_server_task_work(g_server, 2000);
    return 0;
}

static int test_task_work_updates_last_timeout_scan(void) {
    setup_server();

    /* Trigger the timeout scan path */
    weather_server_task_work(g_server, 5000);

    TEST_ASSERT(g_server->last_timeout_scan_ms == 5000);
    return 0;
}

static int test_task_work_no_scan_below_threshold(void) {
    setup_server();

    /* mon_time=500: below 1000ms threshold, scan should NOT run */
    weather_server_task_work(g_server, 500);

    TEST_ASSERT(g_server->last_timeout_scan_ms == 0);
    return 0;
}

static int test_task_work_with_populated_instance_list(void) {
    setup_server();

    HTTPServerConnection conn;
    memset(&conn, 0, sizeof(conn));
    weather_server_on_http_connection(g_server, &conn);

    /* Both I/O poll (no events) and timeout scan run cleanly */
    weather_server_task_work(g_server, 2000);

    TEST_ASSERT(g_server->last_timeout_scan_ms == 2000);
    return 0;
}

/* ============= Main ============= */

int main(void) {
    printf("=== weather_server unit tests ===\n\n");

    /* smw_create_task (used by weather_server_initiate) requires the global
     * SMW to be initialised first. */
    smw_init();

    /* weather_server_initiate_ptr */
    RUN_TEST(test_initiate_ptr_null_server_ptr_returns_minus1);
    RUN_TEST(test_initiate_ptr_success);
    RUN_TEST(test_initiate_ptr_initialises_instances_list);
    RUN_TEST(test_initiate_ptr_initialises_epoll_fd);
    RUN_TEST(test_initiate_ptr_initialises_task);
    RUN_TEST(test_initiate_ptr_zeroes_timeout_scan);

    /* weather_server_initiate (stack) + weather_server_dispose (stack) */
    RUN_TEST(test_initiate_stack_returns_zero);

    /* weather_server_dispose_ptr */
    RUN_TEST(test_dispose_ptr_null_no_crash);
    RUN_TEST(test_dispose_ptr_null_server_no_crash);
    RUN_TEST(test_dispose_ptr_sets_pointer_to_null);

    /* weather_server_on_http_connection */
    RUN_TEST(test_on_http_connection_returns_zero);
    RUN_TEST(test_on_http_connection_adds_instance_to_list);
    RUN_TEST(test_on_http_connection_multiple_connections);

    /* weather_server_task_work */
    RUN_TEST(test_task_work_no_crash_empty_list_no_scan);
    RUN_TEST(test_task_work_no_crash_empty_list_with_scan);
    RUN_TEST(test_task_work_updates_last_timeout_scan);
    RUN_TEST(test_task_work_no_scan_below_threshold);
    RUN_TEST(test_task_work_with_populated_instance_list);

    printf("\n=== %d/%d tests passed ===\n", g_tests_passed, g_tests_run);

    smw_dispose();

    if (g_tests_failed > 0) {
        printf("=== %d FAILED ===\n", g_tests_failed);
        return 1;
    }

    return 0;
}
