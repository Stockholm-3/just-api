#ifndef SCHEDULER_SERVICE_H
#define SCHEDULER_SERVICE_H

#include <pthread.h>
#include <signal.h>

typedef struct {
    volatile sig_atomic_t* shutdown_flag;
} SchedulerServiceConfig;

/* Starts scheduler thread */
int scheduler_service_start(pthread_t*                    thread,
                            const SchedulerServiceConfig* config);

/* Stops scheduler and waits */
int scheduler_service_stop(pthread_t thread);

#endif
