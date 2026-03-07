/**
 * test_thread_pool.c - Unit tests for the thread_pool module
 */

#include "thread_pool.h"

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

/* ============= Test Helpers ============= */

/* Each test creates and destroys its own pool — no global pool */
static void teardown(void) {}

/* Shared state written by worker threads, read on main thread.
 * volatile is sufficient: thread_pool_wait_idle() provides the
 * memory barrier (mutex release/acquire) before we read these. */
static volatile int g_done_called;
static volatile int g_done_status;
static volatile int g_work_called;
static volatile int g_task_started;
static volatile int g_task_release;
static volatile int g_remaining_result;

/* Counter array for parallel / many-tasks tests */
#define MAX_TASKS 64
static volatile int g_counters[MAX_TASKS];

/* Order-tracking for FIFO tests */
#define MAX_ORDER 16
static volatile int g_order[MAX_ORDER];
static volatile int g_order_idx;

/* --- Callbacks --- */

static int work_increment(void* arg, ThreadPoolTask* task) {
    (void)task;
    (*(volatile int*)arg)++;
    return 0;
}

static int work_return42(void* arg, ThreadPoolTask* task) {
    (void)arg;
    (void)task;
    return 42;
}

static int work_blocking(void* arg, ThreadPoolTask* task) {
    (void)arg;
    (void)task;
    g_task_started = 1;
    while (!g_task_release) {
        usleep(100);
    }
    return 0;
}

static int work_sleep50ms(void* arg, ThreadPoolTask* task) {
    (void)arg;
    (void)task;
    g_task_started = 1;
    usleep(50000);
    return 0;
}

/* Polls thread_pool_task_is_cancelled() until it turns true, then records it.
 * Used to test cooperative cancellation during execution. */
static int work_cooperative_cancel(void* arg, ThreadPoolTask* task) {
    (void)arg;
    g_task_started = 1;
    while (!thread_pool_task_is_cancelled(task)) {
        usleep(100);
    }
    g_work_called = 1;
    return 0;
}

static int work_check_remaining(void* arg, ThreadPoolTask* task) {
    (void)arg;
    g_remaining_result = thread_pool_task_remaining_ms(task);
    return 0;
}

/* Records submission-order index supplied via arg into g_order[] */
static int work_record_order(void* arg, ThreadPoolTask* task) {
    (void)task;
    int idx      = g_order_idx++;
    g_order[idx] = *(int*)arg;
    return 0;
}

static void done_capture(void* arg, int status) {
    (void)arg;
    g_done_called++;
    g_done_status = status;
}

static void done_capture_arg(void* arg, int status) {
    *(volatile int*)arg = status;
    g_done_called++;
}

/* ============= Lifecycle Tests ============= */

static int test_create_destroy(void) {
    ThreadPool* pool = thread_pool_create(2, 0);
    TEST_ASSERT(pool != NULL);
    thread_pool_destroy(pool);
    return 0;
}

static int test_create_invalid_workers(void) {
    TEST_ASSERT(thread_pool_create(0, 0) == NULL);
    TEST_ASSERT(thread_pool_create(-1, 0) == NULL);
    return 0;
}

static int test_destroy_null(void) {
    thread_pool_destroy(NULL); /* must not crash */
    return 0;
}

static int test_submit_null_pool(void) {
    ThreadPoolTask* t = thread_pool_submit(NULL, NULL, NULL, NULL, NULL, 0, 0);
    TEST_ASSERT(t == NULL);
    return 0;
}

/* ============= Work Execution Tests ============= */

static int test_work_fn_executes(void) {
    ThreadPool*  pool    = thread_pool_create(1, 0);
    volatile int counter = 0;

    thread_pool_submit(pool, work_increment, (void*)&counter, NULL, NULL, 0, 0);
    thread_pool_wait_idle(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ((int)counter, 1);
    return 0;
}

static int test_work_fn_null(void) {
    g_done_called = 0;
    g_done_status = -999;

    ThreadPool* pool = thread_pool_create(1, 0);
    thread_pool_submit(pool, NULL, NULL, done_capture, NULL, 0, 0);
    thread_pool_wait_idle(pool);
    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(g_done_called, 1);
    TEST_ASSERT_EQ(g_done_status, TP_STATUS_OK);
    return 0;
}

static int test_done_fn_receives_status(void) {
    g_done_called = 0;
    g_done_status = -999;

    ThreadPool* pool = thread_pool_create(1, 0);
    thread_pool_submit(pool, work_return42, NULL, done_capture, NULL, 0, 0);
    thread_pool_wait_idle(pool);
    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(g_done_called, 1);
    TEST_ASSERT_EQ(g_done_status, 42);
    return 0;
}

static int test_many_tasks(void) {
    const int    N    = 50;
    ThreadPool*  pool = thread_pool_create(4, 0);
    volatile int counters[50];
    memset((void*)counters, 0, sizeof(counters));

    for (int i = 0; i < N; i++) {
        ThreadPoolTask* t = thread_pool_submit(
            pool, work_increment, (void*)&counters[i], NULL, NULL, 0, 0);
        TEST_ASSERT(t != NULL);
    }

    thread_pool_wait_idle(pool);
    thread_pool_destroy(pool);

    for (int i = 0; i < N; i++) {
        TEST_ASSERT_EQ((int)counters[i], 1);
    }
    return 0;
}

/* ============= Cancellation Tests ============= */

static int test_cancel_queued_task(void) {
    g_done_called  = 0;
    g_done_status  = -999;
    g_task_started = 0;
    g_task_release = 0;

    /* 1 worker: task1 blocks the worker, task2 sits in queue */
    ThreadPool* pool = thread_pool_create(1, 0);

    thread_pool_submit(pool, work_blocking, NULL, NULL, NULL, 0, 0);

    /* Wait until worker picks up task1 */
    while (!g_task_started) {
        usleep(100);
    }

    /* task2 is now queued — cancel it before the worker reaches it */
    ThreadPoolTask* task2 =
        thread_pool_submit(pool, NULL, NULL, done_capture, NULL, 0, 0);
    TEST_ASSERT(task2 != NULL);
    thread_pool_cancel(task2);

    /* Unblock task1 */
    g_task_release = 1;

    thread_pool_wait_idle(pool);
    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(g_done_called, 1);
    TEST_ASSERT_EQ(g_done_status, TP_STATUS_CANCELLED);
    return 0;
}

static int test_cancel_null_task(void) {
    thread_pool_cancel(NULL); /* must not crash */
    return 0;
}

static int test_task_is_cancelled(void) {
    g_work_called  = 0;
    g_task_started = 0;

    ThreadPool* pool = thread_pool_create(1, 0);

    /* Submit a task that polls thread_pool_task_is_cancelled() cooperatively */
    ThreadPoolTask* task = thread_pool_submit(pool, work_cooperative_cancel,
                                              NULL, NULL, NULL, 0, 0);
    TEST_ASSERT(task != NULL);

    /* Wait until work_fn has started, then cancel mid-execution */
    while (!g_task_started) {
        usleep(100);
    }
    thread_pool_cancel(task);

    thread_pool_wait_idle(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(g_work_called, 1);
    return 0;
}

/* ============= Timeout Tests ============= */

static int test_remaining_ms_no_timeout(void) {
    g_remaining_result = -999;

    ThreadPool* pool = thread_pool_create(1, 0);
    thread_pool_submit(pool, work_check_remaining, NULL, NULL, NULL, 0, 0);
    thread_pool_wait_idle(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(g_remaining_result, 0);
    return 0;
}

static int test_remaining_ms_with_timeout(void) {
    g_remaining_result = -999;

    ThreadPool* pool = thread_pool_create(1, 0);
    thread_pool_submit(pool, work_check_remaining, NULL, NULL, NULL, 5000, 0);
    thread_pool_wait_idle(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT(g_remaining_result > 0 && g_remaining_result <= 5000);
    return 0;
}

static int test_timeout_fires(void) {
    g_done_called  = 0;
    g_done_status  = -999;
    g_task_started = 0;

    /* 1 worker: task1 sleeps 50ms, task2 has 1ms timeout — by the time
     * the worker picks it up the deadline will have passed. */
    ThreadPool* pool = thread_pool_create(1, 0);

    thread_pool_submit(pool, work_sleep50ms, NULL, NULL, NULL, 0, 0);

    /* Wait until task1 is actually running */
    while (!g_task_started) {
        usleep(100);
    }

    /* Submit task2 with a 1ms timeout while the worker is busy for ~50ms */
    thread_pool_submit(pool, NULL, NULL, done_capture, NULL, 1, 0);

    thread_pool_wait_idle(pool);
    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(g_done_called, 1);
    TEST_ASSERT_EQ(g_done_status, TP_STATUS_TIMEOUT);
    return 0;
}

/* ============= Stats Tests ============= */

static int test_stats_initial(void) {
    ThreadPool*     pool = thread_pool_create(3, 0);
    ThreadPoolStats s;
    thread_pool_get_stats(pool, &s);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(s.num_workers, 3);
    TEST_ASSERT_EQ(s.active_workers, 0);
    TEST_ASSERT_EQ(s.pending_tasks, 0);
    TEST_ASSERT_EQ(s.completed_tasks, 0);
    return 0;
}

static int test_stats_after_task(void) {
    ThreadPool*  pool    = thread_pool_create(1, 0);
    volatile int counter = 0;

    thread_pool_submit(pool, work_increment, (void*)&counter, NULL, NULL, 0, 0);
    thread_pool_wait_idle(pool);

    ThreadPoolStats s;
    thread_pool_get_stats(pool, &s);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(s.completed_tasks, 1);
    return 0;
}

static int test_stats_completed_includes_cancelled(void) {
    g_task_started = 0;
    g_task_release = 0;

    /* 1 worker: task1 blocks it, task2 is queued and then cancelled */
    ThreadPool* pool = thread_pool_create(1, 0);
    thread_pool_submit(pool, work_blocking, NULL, NULL, NULL, 0, 0);
    while (!g_task_started) {
        usleep(100);
    }

    ThreadPoolTask* t2 = thread_pool_submit(pool, NULL, NULL, NULL, NULL, 0, 0);
    TEST_ASSERT(t2 != NULL);
    thread_pool_cancel(t2);

    g_task_release = 1;
    thread_pool_wait_idle(pool);

    ThreadPoolStats s;
    thread_pool_get_stats(pool, &s);
    thread_pool_destroy(pool);

    /* Both the executed task and the cancelled task must be counted */
    TEST_ASSERT_EQ(s.completed_tasks, 2);
    return 0;
}

static int test_stats_completed_includes_timeout(void) {
    g_task_started = 0;

    /* 1 worker: task1 sleeps 50ms, task2 has 1ms timeout */
    ThreadPool* pool = thread_pool_create(1, 0);
    thread_pool_submit(pool, work_sleep50ms, NULL, NULL, NULL, 0, 0);
    while (!g_task_started) {
        usleep(100);
    }

    thread_pool_submit(pool, NULL, NULL, NULL, NULL, 1, 0);

    thread_pool_wait_idle(pool);

    ThreadPoolStats s;
    thread_pool_get_stats(pool, &s);
    thread_pool_destroy(pool);

    /* Both the executed task and the timed-out task must be counted */
    TEST_ASSERT_EQ(s.completed_tasks, 2);
    return 0;
}

static int test_stats_null(void) {
    ThreadPool*     pool = thread_pool_create(1, 0);
    ThreadPoolStats s;
    thread_pool_get_stats(NULL, &s);   /* must not crash */
    thread_pool_get_stats(pool, NULL); /* must not crash */
    thread_pool_destroy(pool);
    return 0;
}

/* ============= Completion Queue Tests ============= */

static int test_process_completions(void) {
    g_done_called = 0;
    g_done_status = -999;

    volatile int sentinel = 0;
    ThreadPool*  pool     = thread_pool_create(1, 0);

    thread_pool_submit(pool, work_return42, NULL, done_capture_arg,
                       (void*)&sentinel, 0, 0);
    thread_pool_wait_idle(pool);
    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(g_done_called, 1);
    TEST_ASSERT_EQ((int)sentinel, 42);
    return 0;
}

static int test_process_completions_null(void) {
    int n = thread_pool_process_completions(NULL);
    TEST_ASSERT_EQ(n, 0);
    return 0;
}

static int test_process_completions_count(void) {
    const int   N    = 8;
    ThreadPool* pool = thread_pool_create(4, 0);

    volatile int statuses[8];
    memset((void*)statuses, 0, sizeof(statuses));

    for (int i = 0; i < N; i++) {
        thread_pool_submit(pool, NULL, NULL, done_capture_arg,
                           (void*)&statuses[i], 0, 0);
    }
    thread_pool_wait_idle(pool);
    int n = thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(n, N);
    for (int i = 0; i < N; i++) {
        TEST_ASSERT_EQ((int)statuses[i], TP_STATUS_OK);
    }
    return 0;
}

/* ============= Backpressure Tests ============= */

static int test_max_pending_rejects(void) {
    g_task_started = 0;
    g_task_release = 0;

    /* 1 worker, max 1 pending */
    ThreadPool* pool = thread_pool_create(1, 1);

    /* task1 blocks the worker */
    thread_pool_submit(pool, work_blocking, NULL, NULL, NULL, 0, 0);
    while (!g_task_started) {
        usleep(100);
    }

    /* task2 fills the pending slot */
    ThreadPoolTask* t2 = thread_pool_submit(pool, NULL, NULL, NULL, NULL, 0, 0);
    TEST_ASSERT(t2 != NULL);

    /* task3 must be rejected — queue is full */
    ThreadPoolTask* t3 = thread_pool_submit(pool, NULL, NULL, NULL, NULL, 0, 0);
    TEST_ASSERT(t3 == NULL);

    g_task_release = 1;
    thread_pool_wait_idle(pool);
    thread_pool_destroy(pool);
    return 0;
}

/* ============= Concurrency Regression Tests ============= */

/* Regression: wait_idle() + process_completions() must always dispatch
 * done_fn regardless of spurious wakeups or thread scheduling.
 * Previously, idle_cond could fire before done.tasks.push(), causing
 * process_completions to find an empty queue and silently drop the callback.
 * Run many iterations to expose intermittent race conditions. */
static int test_wait_idle_then_process_stress(void) {
    const int ITERS = 200;
    for (int i = 0; i < ITERS; i++) {
        g_done_called    = 0;
        ThreadPool* pool = thread_pool_create(1, 0);
        thread_pool_submit(pool, NULL, NULL, done_capture, NULL, 0, 0);
        thread_pool_wait_idle(pool);
        thread_pool_process_completions(pool);
        thread_pool_destroy(pool);
        if (g_done_called != 1) {
            printf("  iteration %d: done_fn not called\n", i);
            return 1;
        }
    }
    return 0;
}

/* Regression: thread_pool_get_stats() must return a consistent snapshot.
 * Previously, active_workers was read before acquiring work.mutex while
 * pending_tasks was read under it, so a snapshot could show active=0,
 * pending=0 while a task was actively being processed.
 * Verify active=1, pending=3 while 1 worker is blocked and 3 tasks are
 * queued — both fields must be accurate at the same instant. */
static int test_stats_active_pending_consistent(void) {
    g_task_started = 0;
    g_task_release = 0;

    ThreadPool* pool = thread_pool_create(1, 0);
    thread_pool_submit(pool, work_blocking, NULL, NULL, NULL, 0, 0);
    while (!g_task_started) {
        usleep(100);
    }

    /* Queue 3 more tasks while the single worker is blocked */
    for (int i = 0; i < 3; i++) {
        ThreadPoolTask* t =
            thread_pool_submit(pool, NULL, NULL, NULL, NULL, 0, 0);
        TEST_ASSERT(t != NULL);
    }

    ThreadPoolStats s;
    thread_pool_get_stats(pool, &s);

    g_task_release = 1;
    thread_pool_wait_idle(pool);
    thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(s.active_workers, 1);
    TEST_ASSERT_EQ(s.pending_tasks, 3);
    return 0;
}

/* ============= Queue Behaviour Tests ============= */

/* WorkQueue: tasks must execute in FIFO order with a single worker. */
static int test_work_queue_fifo(void) {
    const int N      = 5;
    int       ids[5] = {0, 1, 2, 3, 4};

    g_order_idx = 0;
    memset((void*)g_order, -1, sizeof(g_order));

    /* 1 worker guarantees serial execution — FIFO ordering observable */
    ThreadPool* pool = thread_pool_create(1, 0);
    for (int i = 0; i < N; i++) {
        ThreadPoolTask* t = thread_pool_submit(pool, work_record_order, &ids[i],
                                               NULL, NULL, 0, 0);
        TEST_ASSERT(t != NULL);
    }
    thread_pool_wait_idle(pool);
    thread_pool_destroy(pool);

    for (int i = 0; i < N; i++) {
        TEST_ASSERT_EQ((int)g_order[i], i);
    }
    return 0;
}

/* WorkQueue: tasks already queued when destroy is called must still execute
 * (workers drain the queue before honouring the shutdown flag). */
static int test_work_queue_drains_on_destroy(void) {
    g_task_started = 0;
    g_task_release = 0;
    g_order_idx    = 0;
    memset((void*)g_order, 0, sizeof(g_order));

    int ids[3] = {0, 1, 2};

    /* 1 worker: task0 blocks it while task1..2 pile up in the queue */
    ThreadPool* pool = thread_pool_create(1, 0);
    thread_pool_submit(pool, work_blocking, NULL, NULL, NULL, 0, 0);

    while (!g_task_started) {
        usleep(100);
    }

    for (int i = 0; i < 3; i++) {
        ThreadPoolTask* t = thread_pool_submit(pool, work_record_order, &ids[i],
                                               NULL, NULL, 0, 0);
        TEST_ASSERT(t != NULL);
    }

    /* Unblock task0 then destroy — destroy must wait for all tasks */
    g_task_release = 1;
    thread_pool_wait_idle(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(g_order_idx, 3);
    return 0;
}

/* WorkQueue: HIGH-priority tasks must execute before LOW-priority tasks
 * even when LOW tasks were submitted first. */
static int test_work_queue_priority(void) {
    g_task_started = 0;
    g_task_release = 0;
    g_order_idx    = 0;
    memset((void*)g_order, -1, sizeof(g_order));

    int lo_ids[3] = {10, 11, 12};
    int hi_ids[3] = {20, 21, 22};

    /* 1 worker: blocking task holds it while we queue lo and hi tasks */
    ThreadPool* pool = thread_pool_create(1, 0);
    thread_pool_submit(pool, work_blocking, NULL, NULL, NULL, 0, 0);

    while (!g_task_started) {
        usleep(100);
    }

    /* Submit LOW-priority tasks first */
    for (int i = 0; i < 3; i++) {
        ThreadPoolTask* t = thread_pool_submit(pool, work_record_order,
                                               &lo_ids[i], NULL, NULL, 0, -1);
        TEST_ASSERT(t != NULL);
    }

    /* Submit HIGH-priority tasks after — they should run first */
    for (int i = 0; i < 3; i++) {
        ThreadPoolTask* t = thread_pool_submit(pool, work_record_order,
                                               &hi_ids[i], NULL, NULL, 0, 0);
        TEST_ASSERT(t != NULL);
    }

    g_task_release = 1;
    thread_pool_wait_idle(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(g_order_idx, 6);
    /* First 3 slots: HIGH tasks (20, 21, 22) */
    TEST_ASSERT_EQ((int)g_order[0], 20);
    TEST_ASSERT_EQ((int)g_order[1], 21);
    TEST_ASSERT_EQ((int)g_order[2], 22);
    /* Last 3 slots: LOW tasks (10, 11, 12) */
    TEST_ASSERT_EQ((int)g_order[3], 10);
    TEST_ASSERT_EQ((int)g_order[4], 11);
    TEST_ASSERT_EQ((int)g_order[5], 12);
    return 0;
}

/* CompletionQueue: done tasks accumulate in the queue and are NOT dispatched
 * until thread_pool_process_completions() is called. */
static int test_completion_queue_accumulates(void) {
    const int N   = 5;
    g_done_called = 0;

    ThreadPool* pool = thread_pool_create(2, 0);
    for (int i = 0; i < N; i++) {
        thread_pool_submit(pool, NULL, NULL, done_capture, NULL, 0, 0);
    }
    thread_pool_wait_idle(pool);

    /* Callbacks must NOT have fired yet */
    TEST_ASSERT_EQ(g_done_called, 0);

    int n = thread_pool_process_completions(pool);
    thread_pool_destroy(pool);

    TEST_ASSERT_EQ(n, N);
    TEST_ASSERT_EQ(g_done_called, N);
    return 0;
}

/* CompletionQueue: unprocessed done tasks must be freed by destroy without
 * invoking the callbacks (no use-after-free, no callback surprise). */
static int test_completion_queue_cleared_on_destroy(void) {
    const int N   = 5;
    g_done_called = 0;

    ThreadPool* pool = thread_pool_create(2, 0);
    for (int i = 0; i < N; i++) {
        thread_pool_submit(pool, NULL, NULL, done_capture, NULL, 0, 0);
    }
    thread_pool_wait_idle(pool);

    /* Destroy WITHOUT calling process_completions first */
    thread_pool_destroy(pool);

    /* Callbacks must never have been invoked */
    TEST_ASSERT_EQ(g_done_called, 0);
    return 0;
}

/* ============= Main ============= */

int main(void) {
    printf("=== thread_pool unit tests ===\n\n");

    /* Lifecycle */
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_create_invalid_workers);
    RUN_TEST(test_destroy_null);
    RUN_TEST(test_submit_null_pool);

    /* Work execution */
    RUN_TEST(test_work_fn_executes);
    RUN_TEST(test_work_fn_null);
    RUN_TEST(test_done_fn_receives_status);
    RUN_TEST(test_many_tasks);

    /* Cancellation */
    RUN_TEST(test_cancel_queued_task);
    RUN_TEST(test_cancel_null_task);
    RUN_TEST(test_task_is_cancelled);

    /* Timeout */
    RUN_TEST(test_remaining_ms_no_timeout);
    RUN_TEST(test_remaining_ms_with_timeout);
    RUN_TEST(test_timeout_fires);

    /* Stats */
    RUN_TEST(test_stats_initial);
    RUN_TEST(test_stats_after_task);
    RUN_TEST(test_stats_completed_includes_cancelled);
    RUN_TEST(test_stats_completed_includes_timeout);
    RUN_TEST(test_stats_null);

    /* Completion queue */
    RUN_TEST(test_process_completions);
    RUN_TEST(test_process_completions_null);
    RUN_TEST(test_process_completions_count);

    /* Backpressure */
    RUN_TEST(test_max_pending_rejects);

    /* Concurrency regressions */
    RUN_TEST(test_wait_idle_then_process_stress);
    RUN_TEST(test_stats_active_pending_consistent);

    /* Queue behaviour */
    RUN_TEST(test_work_queue_fifo);
    RUN_TEST(test_work_queue_drains_on_destroy);
    RUN_TEST(test_work_queue_priority);
    RUN_TEST(test_completion_queue_accumulates);
    RUN_TEST(test_completion_queue_cleared_on_destroy);

    printf("\n=== %d/%d tests passed ===\n", g_tests_passed, g_tests_run);

    if (g_tests_failed > 0) {
        printf("=== %d FAILED ===\n", g_tests_failed);
        return 1;
    }
    return 0;
}
