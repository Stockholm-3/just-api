/**
 * test_process_health_check.c - Unit tests for process_health_check()
 */

#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "watchdog/process.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
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

/* ============= Fake server helper ============= */

/*
 * Creates a listening socket (bound to port 0, so the OS picks a free port),
 * forks a child that accept()s one connection and optionally delays before
 * responding.  The parent learns the real port via getsockname() BEFORE
 * fork() to avoid any race.
 *
 * response   – string to write after accept(); NULL = close immediately
 * out_port   – receives the port the server is listening on
 * delay_secs – child sleeps this many seconds before accept() (0 = no delay)
 *
 * Returns the child PID.  Caller must kill() + waitpid() after the check.
 */
static pid_t start_fake_server(const char* response, int* out_port,
                               int delay_secs) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
    listen(lfd, 1);

    socklen_t len = sizeof(addr);
    getsockname(lfd, (struct sockaddr*)&addr, &len);
    *out_port = ntohs(addr.sin_port); /* known before fork */

    pid_t pid = fork();
    if (pid == 0) {
        /* Child */
        if (delay_secs > 0)
            sleep((unsigned)delay_secs);
        int cfd = accept(lfd, NULL, NULL);
        if (cfd >= 0) {
            if (response) {
                ssize_t n = write(cfd, response, strlen(response));
                (void)n;
            }
            close(cfd);
        }
        close(lfd);
        _exit(0);
    }
    /* Parent */
    close(lfd);
    return pid;
}

/* ============= Tests ============= */

static int test_hc_200_returns_0(void) {
    int   port;
    pid_t srv    = start_fake_server("HTTP/1.0 200 OK\r\n\r\n", &port, 0);
    int   result = process_health_check("127.0.0.1", port, 2000);
    kill(srv, SIGKILL);
    waitpid(srv, NULL, 0);
    TEST_ASSERT_EQ(result, 0);
    return 0;
}

static int test_hc_503_returns_neg1(void) {
    int   port;
    pid_t srv =
        start_fake_server("HTTP/1.0 503 Service Unavailable\r\n\r\n", &port, 0);
    int result = process_health_check("127.0.0.1", port, 2000);
    kill(srv, SIGKILL);
    waitpid(srv, NULL, 0);
    TEST_ASSERT_EQ(result, -1);
    return 0;
}

static int test_hc_404_returns_neg1(void) {
    int   port;
    pid_t srv = start_fake_server("HTTP/1.0 404 Not Found\r\n\r\n", &port, 0);
    int   result = process_health_check("127.0.0.1", port, 2000);
    kill(srv, SIGKILL);
    waitpid(srv, NULL, 0);
    TEST_ASSERT_EQ(result, -1);
    return 0;
}

static int test_hc_connection_refused(void) {
    /* Bind to get a free port, then close — nothing is listening */
    int                lfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
    socklen_t len = sizeof(addr);
    getsockname(lfd, (struct sockaddr*)&addr, &len);
    int port = ntohs(addr.sin_port);
    close(lfd); /* nobody listening now */

    int result = process_health_check("127.0.0.1", port, 2000);
    TEST_ASSERT_EQ(result, -1);
    return 0;
}

static int test_hc_timeout_no_response(void) {
    /* Child sleeps 10 s before accepting — health check times out first */
    int   port;
    pid_t srv    = start_fake_server("HTTP/1.0 200 OK\r\n\r\n", &port, 10);
    int   result = process_health_check("127.0.0.1", port, 500);
    kill(srv, SIGKILL);
    waitpid(srv, NULL, 0);
    TEST_ASSERT_EQ(result, -1);
    return 0;
}

static int test_hc_connection_closed_immediately(void) {
    /* Child accepts and immediately closes without writing anything */
    int   port;
    pid_t srv    = start_fake_server(NULL, &port, 0);
    int   result = process_health_check("127.0.0.1", port, 2000);
    kill(srv, SIGKILL);
    waitpid(srv, NULL, 0);
    TEST_ASSERT_EQ(result, -1);
    return 0;
}

static int test_hc_malformed_response(void) {
    int   port;
    pid_t srv    = start_fake_server("GARBAGE DATA\r\n\r\n", &port, 0);
    int   result = process_health_check("127.0.0.1", port, 2000);
    kill(srv, SIGKILL);
    waitpid(srv, NULL, 0);
    TEST_ASSERT_EQ(result, -1);
    return 0;
}

/* ============= main ============= */

int main(void) {
    printf("=== process_health_check Tests ===\n\n");

    RUN_TEST(test_hc_200_returns_0);
    RUN_TEST(test_hc_503_returns_neg1);
    RUN_TEST(test_hc_404_returns_neg1);
    RUN_TEST(test_hc_connection_refused);
    RUN_TEST(test_hc_timeout_no_response);
    RUN_TEST(test_hc_connection_closed_immediately);
    RUN_TEST(test_hc_malformed_response);

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return g_tests_failed > 0 ? 1 : 0;
}
