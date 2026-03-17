/**
 * test_thread_pool_pipe.c - Unit tests for the self-pipe notify fd
 *
 * Verifies that thread_pool_get_notify_fd() returns a valid pipe read-end,
 * that workers write to it exactly when a done callback is queued, and that
 * the drain-then-process pattern (as used by the epoll loop) works correctly.
 */

#include "thread_pool.h"

#include <poll.h>
#include <stdio.h>
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
        if (_result == 0) {                                                    \
            g_tests_passed++;                                                  \
            printf("[PASS] %s\n", #fn);                                        \
        } else {                                                               \
            g_tests_failed++;                                                  \
            printf("[FAIL] %s\n", #fn);                                        \
        }                                                                      \
    } while (0)

/* ============= Helpers ============= */

static volatile int g_done_called;

static void done_capture(void* arg, int status) {
    (void)arg;
    (void)status;
    g_done_called++;
}

/* Returns non-zero if fd has POLLIN ready right now (0-timeout poll). */
static int poll_readable(int fd) {
    struct pollfd pfd = {fd, POLLIN, 0};
    poll(&pfd, 1, 0);
    return (pfd.revents & POLLIN) != 0;
}

/* ============= Tests ============= */

static int test_notify_fd_valid(void) {
    TEST_ASSERT_EQ(thread_pool_get_notify_fd(NULL), -1);

    ThreadPool* pool = thread_pool_create(1, 0);
    TEST_ASSERT(pool != NULL);
    TEST_ASSERT(thread_pool_get_notify_fd(pool) >= 0);
    thread_pool_destroy(pool);
    return 0;
}

static int test_notify_fd_readable_after_completion(void) {
    g_done_called = 0;

    ThreadPool* pool      = thread_pool_create(1, 0);
    int         notify_fd = thread_pool_get_notify_fd(pool);

    thread_pool_submit(pool, NULL, NULL, done_capture, NULL, 0, 0);
    thread_pool_wait_idle(pool);

    /* Pipe must be readable — worker wrote a byte when queuing the done cb */
    TEST_ASSERT(poll_readable(notify_fd));

    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);
    return 0;
}

static int test_notify_fd_not_readable_without_done_fn(void) {
    ThreadPool* pool      = thread_pool_create(1, 0);
    int         notify_fd = thread_pool_get_notify_fd(pool);

    /* No done_fn → worker deletes task without touching the pipe */
    thread_pool_submit(pool, NULL, NULL, NULL, NULL, 0, 0);
    thread_pool_wait_idle(pool);

    TEST_ASSERT(!poll_readable(notify_fd));

    thread_pool_destroy(pool);
    return 0;
}

static int test_notify_fd_drain_and_process(void) {
    const int N   = 3;
    g_done_called = 0;

    ThreadPool* pool      = thread_pool_create(2, 0);
    int         notify_fd = thread_pool_get_notify_fd(pool);

    for (int i = 0; i < N; i++) {
        thread_pool_submit(pool, NULL, NULL, done_capture, NULL, 0, 0);
    }
    thread_pool_wait_idle(pool);

    /* Simulate epoll: pipe is readable, drain it, then process completions */
    TEST_ASSERT(poll_readable(notify_fd));

    char buf[64];
    while (read(notify_fd, buf, sizeof(buf)) > 0) {
    }

    int dispatched = thread_pool_process_completions(pool);

    TEST_ASSERT_EQ(dispatched, N);
    TEST_ASSERT_EQ(g_done_called, N);
    /* Pipe must be empty after draining */
    TEST_ASSERT(!poll_readable(notify_fd));

    thread_pool_destroy(pool);
    return 0;
}

static int test_notify_fd_bytes_equal_completions(void) {
    const int N   = 5;
    g_done_called = 0;

    ThreadPool* pool      = thread_pool_create(2, 0);
    int         notify_fd = thread_pool_get_notify_fd(pool);

    for (int i = 0; i < N; i++) {
        thread_pool_submit(pool, NULL, NULL, done_capture, NULL, 0, 0);
    }
    thread_pool_wait_idle(pool);

    /* Count bytes: one byte written per queued done callback */
    int  total = 0;
    char buf[64];
    int  n;
    while ((n = read(notify_fd, buf, sizeof(buf))) > 0) {
        total += n;
    }
    TEST_ASSERT_EQ(total, N);

    int dispatched = thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(dispatched, N);
    TEST_ASSERT_EQ(g_done_called, N);
    return 0;
}

/* ============= Main ============= */

int main(void) {
    printf("=== thread_pool notify fd (self-pipe) tests ===\n\n");

    RUN_TEST(test_notify_fd_valid);
    RUN_TEST(test_notify_fd_readable_after_completion);
    RUN_TEST(test_notify_fd_not_readable_without_done_fn);
    RUN_TEST(test_notify_fd_drain_and_process);
    RUN_TEST(test_notify_fd_bytes_equal_completions);

    printf("\n=== %d/%d tests passed ===\n", g_tests_passed, g_tests_run);

    if (g_tests_failed > 0) {
        printf("=== %d FAILED ===\n", g_tests_failed);
        return 1;
    }
    return 0;
}
