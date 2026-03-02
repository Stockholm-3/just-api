/**
 * test_thread_pool_exception.cpp
 *
 * Regression test: a C++ exception thrown from work_fn must NOT call
 * std::terminate.  The worker thread must survive and invoke done_fn with
 * TP_STATUS_ERROR.
 *
 * This is a standalone C++ binary because the test_thread_pool.c suite is
 * compiled as C and cannot throw C++ exceptions.
 */

#include "thread_pool.h"

#include <cstdio>
#include <stdexcept>
#include <unistd.h>

static volatile int g_done_called = 0;
static volatile int g_done_status = 0;

static void done_capture(void* /*arg*/, int status) {
    g_done_called++;
    g_done_status = status;
}

/* Throws a C++ exception — previously caused std::terminate */
static int work_throw(void* /*arg*/, ThreadPoolTask* /*task*/) {
    throw std::runtime_error("intentional test exception");
    return 0;
}

/* A normal task submitted after the throwing one — verifies the worker
 * thread is still alive and functional after catching the exception. */
static volatile int g_normal_ran = 0;
static int          work_normal(void* /*arg*/, ThreadPoolTask* /*task*/) {
    g_normal_ran = 1;
    return 0;
}

static int run_test(void) {
    g_done_called = 0;
    g_done_status = 0;
    g_normal_ran  = 0;

    ThreadPool* pool = thread_pool_create(1, 0);
    if (!pool) {
        printf("  FAIL: thread_pool_create returned NULL\n");
        return 1;
    }

    /* Task 1: throws — should be caught, worker must survive */
    thread_pool_submit(pool, work_throw, NULL, done_capture, NULL, 0, 0);
    /* Task 2: normal — verifies worker is still alive */
    thread_pool_submit(pool, work_normal, NULL, NULL, NULL, 0, 0);

    thread_pool_wait_idle(pool);
    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    if (g_done_called != 1) {
        printf("  FAIL: done_fn call count: expected 1, got %d\n",
               g_done_called);
        return 1;
    }
    if (g_done_status != TP_STATUS_ERROR) {
        printf("  FAIL: done status: expected TP_STATUS_ERROR (%d), got %d\n",
               TP_STATUS_ERROR, g_done_status);
        return 1;
    }
    if (!g_normal_ran) {
        printf("  FAIL: normal task after exception did not execute\n");
        return 1;
    }
    return 0;
}

int main(void) {
    printf("=== thread_pool exception tests ===\n\n");

    int result = run_test();
    if (result == 0) {
        printf("[PASS] test_work_fn_exception\n");
    } else {
        printf("[FAIL] test_work_fn_exception\n");
    }

    printf("\n=== %d/1 tests passed ===\n", result == 0 ? 1 : 0);
    return result;
}
