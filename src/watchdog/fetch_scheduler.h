#ifndef FETCH_SCHEDULER_H
#define FETCH_SCHEDULER_H

#include <pthread.h>
#include <signal.h>

typedef struct {
    volatile sig_atomic_t* shutdown_flag;
} SchedulerServiceConfig;

/* Starts scheduler thread */
int fetch_scheduler_start(pthread_t*                    thread,
                          const SchedulerServiceConfig* config);

/* Stops scheduler and waits */
int fetch_scheduler_stop(pthread_t thread);

#endif // FETCH_SCHEDULER_H
