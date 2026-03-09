/**
 * test_weather_server_instance.c - Unit tests for weather_server_instance
 * module
 *
 * Tests every function in weather_server_instance.c:
 *   - weather_server_instance_initiate
 *   - weather_server_instance_initiate_ptr
 *   - weather_server_instance_on_request  (internal, declared extern)
 *   - weather_server_instance_work
 *   - weather_server_instance_timeout_check
 *   - weather_server_instance_dispose
 *   - weather_server_instance_dispose_ptr
 */

#include "http_server_connection.h"
#include "weather_server_instance.h"

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
        if (_result == 0) {                                                    \
            g_tests_passed++;                                                  \
            printf("[PASS] %s\n", #fn);                                        \
        } else {                                                               \
            g_tests_failed++;                                                  \
            printf("[FAIL] %s\n", #fn);                                        \
        }                                                                      \
    } while (0)

/* ============= Internal function declaration ============= */

/**
 * weather_server_instance_on_request is defined without static in
 * weather_server_instance.c, so it is accessible via extern declaration.
 */
extern int weather_server_instance_on_request(void* context);

/* ============= Helpers ============= */

/**
 * Return a zero-initialised HTTPServerConnection with method, request_path,
 * and state set so that on_request can route the request and
 * http_server_connection_respond will accept the response.
 */
static HTTPServerConnection make_conn(const char* method, const char* path) {
    HTTPServerConnection conn;
    memset(&conn, 0, sizeof(conn));
    conn.method       = (char*)method;
    conn.request_path = (char*)path;
    conn.state        = HTTP_SERVER_CONNECTION_STATE_WAIT_RESPONSE;
    return conn;
}

/* ============= weather_server_instance_initiate ============= */

static int test_initiate_sets_connection(void) {
    HTTPServerConnection  conn;
    WeatherServerInstance inst;
    memset(&conn, 0, sizeof(conn));

    int result = weather_server_instance_initiate(&inst, &conn);

    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT(inst.connection == &conn);
    return 0;
}

static int test_initiate_registers_callback_context(void) {
    HTTPServerConnection  conn;
    WeatherServerInstance inst;
    memset(&conn, 0, sizeof(conn));

    weather_server_instance_initiate(&inst, &conn);

    /* http_server_connection_set_callback stores instance as context */
    TEST_ASSERT(conn.context == &inst);
    return 0;
}

static int test_initiate_registers_callback_fn(void) {
    HTTPServerConnection  conn;
    WeatherServerInstance inst;
    memset(&conn, 0, sizeof(conn));

    weather_server_instance_initiate(&inst, &conn);

    TEST_ASSERT(conn.onRequest != NULL);
    return 0;
}

/* ============= weather_server_instance_initiate_ptr ============= */

static int test_initiate_ptr_null_instance_ptr_returns_minus1(void) {
    /* instance_ptr is NULL → should return -1 without crashing */
    int result = weather_server_instance_initiate_ptr(NULL, NULL);
    TEST_ASSERT_EQ(result, -1);
    return 0;
}

static int test_initiate_ptr_success(void) {
    HTTPServerConnection   conn;
    WeatherServerInstance* inst = NULL;
    memset(&conn, 0, sizeof(conn));

    int result = weather_server_instance_initiate_ptr(&conn, &inst);

    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT(inst != NULL);
    TEST_ASSERT(inst->connection == &conn);

    free(inst);
    return 0;
}

static int test_initiate_ptr_sets_callback(void) {
    HTTPServerConnection   conn;
    WeatherServerInstance* inst = NULL;
    memset(&conn, 0, sizeof(conn));

    weather_server_instance_initiate_ptr(&conn, &inst);

    TEST_ASSERT(conn.onRequest != NULL);
    TEST_ASSERT(conn.context == inst);

    free(inst);
    return 0;
}

/* ============= weather_server_instance_on_request ============= */

static int test_on_request_unknown_route_returns_zero(void) {
    HTTPServerConnection  conn = make_conn("GET", "/no_such_endpoint");
    WeatherServerInstance inst;
    weather_server_instance_initiate(&inst, &conn);

    int result = weather_server_instance_on_request(&inst);

    /* handle_not_found → send_json_message → returns 0 on success */
    TEST_ASSERT_EQ(result, 0);
    /* Response buffer should be populated */
    TEST_ASSERT(conn.write_buffer != NULL);

    free(conn.write_buffer);
    return 0;
}

static int test_on_request_get_health_routes_correctly(void) {
    HTTPServerConnection  conn = make_conn("GET", "/health");
    WeatherServerInstance inst;
    weather_server_instance_initiate(&inst, &conn);

    int result = weather_server_instance_on_request(&inst);

    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT(conn.write_buffer != NULL);

    free(conn.write_buffer);
    return 0;
}

static int test_on_request_unknown_method_returns_zero(void) {
    /* No route matches DELETE /health → falls through to handle_not_found */
    HTTPServerConnection  conn = make_conn("DELETE", "/health");
    WeatherServerInstance inst;
    weather_server_instance_initiate(&inst, &conn);

    int result = weather_server_instance_on_request(&inst);

    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT(conn.write_buffer != NULL);

    free(conn.write_buffer);
    return 0;
}

static int test_on_request_invocable_via_registered_callback(void) {
    /* Verify the callback stored on the connection calls on_request */
    HTTPServerConnection  conn = make_conn("GET", "/health");
    WeatherServerInstance inst;
    weather_server_instance_initiate(&inst, &conn);

    int result = conn.onRequest(conn.context);

    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT(conn.write_buffer != NULL);

    free(conn.write_buffer);
    return 0;
}

/* ============= weather_server_instance_work ============= */

static int test_work_no_crash_mon_time_zero(void) {
    WeatherServerInstance inst;
    memset(&inst, 0, sizeof(inst));
    weather_server_instance_work(&inst, 0);
    return 0;
}

static int test_work_no_crash_large_mon_time(void) {
    WeatherServerInstance inst;
    memset(&inst, 0, sizeof(inst));
    weather_server_instance_work(&inst, 9999999);
    return 0;
}

/* ============= weather_server_instance_timeout_check ============= */

static int test_timeout_check_no_crash_mon_time_zero(void) {
    WeatherServerInstance inst;
    memset(&inst, 0, sizeof(inst));
    weather_server_instance_timeout_check(&inst, 0);
    return 0;
}

static int test_timeout_check_no_crash_large_mon_time(void) {
    WeatherServerInstance inst;
    memset(&inst, 0, sizeof(inst));
    weather_server_instance_timeout_check(&inst, 9999999);
    return 0;
}

/* ============= weather_server_instance_dispose ============= */

static int test_dispose_no_crash(void) {
    WeatherServerInstance inst;
    memset(&inst, 0, sizeof(inst));
    weather_server_instance_dispose(&inst);
    return 0;
}

/* ============= weather_server_instance_dispose_ptr ============= */

static int test_dispose_ptr_null_no_crash(void) {
    weather_server_instance_dispose_ptr(NULL);
    return 0;
}

static int test_dispose_ptr_null_instance_no_crash(void) {
    WeatherServerInstance* inst = NULL;
    weather_server_instance_dispose_ptr(&inst);
    return 0;
}

static int test_dispose_ptr_sets_pointer_to_null(void) {
    HTTPServerConnection   conn;
    WeatherServerInstance* inst = NULL;
    memset(&conn, 0, sizeof(conn));

    weather_server_instance_initiate_ptr(&conn, &inst);
    TEST_ASSERT(inst != NULL);

    weather_server_instance_dispose_ptr(&inst);
    TEST_ASSERT(inst == NULL);
    return 0;
}

/* ============= Main ============= */

int main(void) {
    printf("=== weather_server_instance unit tests ===\n\n");

    /* weather_server_instance_initiate */
    RUN_TEST(test_initiate_sets_connection);
    RUN_TEST(test_initiate_registers_callback_context);
    RUN_TEST(test_initiate_registers_callback_fn);

    /* weather_server_instance_initiate_ptr */
    RUN_TEST(test_initiate_ptr_null_instance_ptr_returns_minus1);
    RUN_TEST(test_initiate_ptr_success);
    RUN_TEST(test_initiate_ptr_sets_callback);

    /* weather_server_instance_on_request */
    RUN_TEST(test_on_request_unknown_route_returns_zero);
    RUN_TEST(test_on_request_get_health_routes_correctly);
    RUN_TEST(test_on_request_unknown_method_returns_zero);
    RUN_TEST(test_on_request_invocable_via_registered_callback);

    /* weather_server_instance_work */
    RUN_TEST(test_work_no_crash_mon_time_zero);
    RUN_TEST(test_work_no_crash_large_mon_time);

    /* weather_server_instance_timeout_check */
    RUN_TEST(test_timeout_check_no_crash_mon_time_zero);
    RUN_TEST(test_timeout_check_no_crash_large_mon_time);

    /* weather_server_instance_dispose */
    RUN_TEST(test_dispose_no_crash);

    /* weather_server_instance_dispose_ptr */
    RUN_TEST(test_dispose_ptr_null_no_crash);
    RUN_TEST(test_dispose_ptr_null_instance_no_crash);
    RUN_TEST(test_dispose_ptr_sets_pointer_to_null);

    printf("\n=== %d/%d tests passed ===\n", g_tests_passed, g_tests_run);

    if (g_tests_failed > 0) {
        printf("=== %d FAILED ===\n", g_tests_failed);
        return 1;
    }

    return 0;
}
