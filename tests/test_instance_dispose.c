/**
 * test_instance_dispose.c - Unit tests for weather_server_instance_dispose()
 *
 * Verifies that dispose():
 *   - is a no-op when connection is NULL
 *   - is a no-op for states other than WAIT_RESPONSE
 *   - queues a 503 response when state is WAIT_RESPONSE
 */

#include "http/http_server/http_server_connection.h"
#include "utils/smw.h"
#include "weather/weather_server_instance.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
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

/* ============= Per-test state ============= */

/*
 * sv[0] is passed to http_server_connection_initiate() as the socket fd.
 * sv[1] is the "client" side — kept open so the connection doesn't get an
 * immediate EOF on the first read.
 * Initialised to -1 so teardown() can guard close() safely even when
 * setup_connection() was never called (e.g. in test_dispose_null_connection).
 */
static int                  sv[2] = {-1, -1};
static HTTPServerConnection conn;

static void setup_connection(void) {
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    http_server_connection_initiate(&conn, sv[0]);
}

static void teardown(void) {
    /* http_server_connection_dispose has a guard: if !conn.task it's a noop */
    http_server_connection_dispose(&conn);
    if (sv[1] >= 0) {
        close(sv[1]);
        sv[1] = -1;
    }
    sv[0] = -1; /* fd already closed by http_server_connection_dispose */
    memset(&conn, 0, sizeof(conn));
}

/* ============= Tests ============= */

static int test_dispose_null_connection(void) {
    /* dispose() must not crash when connection pointer is NULL */
    WeatherServerInstance inst;
    memset(&inst, 0, sizeof(inst));
    inst.connection = NULL;

    weather_server_instance_dispose(&inst); /* must not crash */
    return 0;
}

static int test_dispose_state_receive_headers(void) {
    setup_connection();
    conn.state = HTTP_SERVER_CONNECTION_STATE_RECEIVE_HEADERS;

    WeatherServerInstance inst = {.connection = &conn};
    weather_server_instance_dispose(&inst);

    /* No response should have been queued */
    TEST_ASSERT(conn.write_buffer == NULL);
    return 0;
}

static int test_dispose_state_send(void) {
    setup_connection();
    conn.state = HTTP_SERVER_CONNECTION_STATE_SEND;

    WeatherServerInstance inst = {.connection = &conn};
    weather_server_instance_dispose(&inst);

    TEST_ASSERT(conn.write_buffer == NULL);
    return 0;
}

static int test_dispose_state_wait_response_sends_503(void) {
    setup_connection();
    conn.state = HTTP_SERVER_CONNECTION_STATE_WAIT_RESPONSE;

    WeatherServerInstance inst = {.connection = &conn};
    weather_server_instance_dispose(&inst);

    /* A 503 response must have been buffered */
    TEST_ASSERT(conn.write_buffer != NULL);
    TEST_ASSERT(strstr((char*)conn.write_buffer, "503") != NULL);
    TEST_ASSERT(strstr((char*)conn.write_buffer, "Retry-After") != NULL);
    return 0;
}

/* ============= main ============= */

int main(void) {
    /* smw_init() must be called once before any http_server_connection_initiate
     * because initiate() calls smw_create_task() which needs the global SMW. */
    if (smw_init() != 0) {
        fprintf(stderr, "smw_init() failed\n");
        return 1;
    }

    printf("=== weather_server_instance_dispose Tests ===\n\n");

    RUN_TEST(test_dispose_null_connection);
    RUN_TEST(test_dispose_state_receive_headers);
    RUN_TEST(test_dispose_state_send);
    RUN_TEST(test_dispose_state_wait_response_sends_503);

    smw_dispose();

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return g_tests_failed > 0 ? 1 : 0;
}
