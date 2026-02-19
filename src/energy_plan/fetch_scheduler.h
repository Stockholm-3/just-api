#ifndef FETCH_SCHEDULER_H
#define FETCH_SCHEDULER_H

#include <pthread.h>
#include <signal.h>

typedef struct {
    volatile sig_atomic_t* shutdown_flag;
    const char* compute_exe; /* Path to compute binary to fork after each fetch.
                                NULL to disable. */
} SchedulerServiceConfig;

/* Starts scheduler thread */
int fetch_scheduler_start(pthread_t*                    thread,
                          const SchedulerServiceConfig* config);

/* Stops scheduler and waits */
int fetch_scheduler_stop(pthread_t thread);

#endif // FETCH_SCHEDULER_H
