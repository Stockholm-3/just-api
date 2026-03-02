#ifndef FETCH_SCHEDULER_H
#define FETCH_SCHEDULER_H

#include "compute.h"
#include "thread_pool.h"

#include <stdint.h>

typedef struct FetchScheduler FetchScheduler;

/**
 * Create an in-process fetch scheduler.
 *
 * Submits an initial fetch+compute immediately (if compute_pool != NULL).
 * Subsequent fetches fire on schedule via fetch_scheduler_smw_callback().
 *
 * @param pool  Thread pool to submit fetch+compute tasks to (low priority).
 *              NULL disables compute submissions (scheduler still tracks time).
 * @param cfg   Compute config (paths). Copied internally.
 * @return Allocated scheduler, or NULL on failure.
 */
FetchScheduler* fetch_scheduler_create(ThreadPool*          pool,
                                       const ComputeConfig* cfg);

/**
 * Destroy the scheduler and free its memory.
 * Does NOT drain the compute pool — call thread_pool_wait_idle() separately.
 */
void fetch_scheduler_destroy(FetchScheduler* sched);

/**
 * SMW-compatible callback. Register with:
 *   smw_create_task(sched, fetch_scheduler_smw_callback)
 *
 * Checks wall-clock UTC time on every event-loop tick and submits
 * fetch+compute tasks to the compute pool when scheduled intervals hit.
 */
void fetch_scheduler_smw_callback(void* context, uint64_t mon_time);

#endif /* FETCH_SCHEDULER_H */
