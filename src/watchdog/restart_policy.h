#ifndef RESTART_POLICY_H
#define RESTART_POLICY_H

#include <time.h>

typedef struct {
    int max_restarts;       // max crash count inside the window
    int restart_window_sec; // sliding window length (seconds)
    int initial_backoff_ms; // first delay after a crash
    int max_backoff_ms;     // ceiling for exponential backoff
} RestartPolicyConfig;

typedef struct {
    int    restart_count;
    time_t window_start;
    int    current_backoff_ms;
} RestartPolicyState;

// Initialises state from config (sets window_start to now).
void restart_policy_init(RestartPolicyState*        state,
                         const RestartPolicyConfig* cfg);

// Returns 1 if another restart is permitted, 0 if the limit has been reached.
// Resets the window and counter if the window has expired.
int restart_policy_should_restart(RestartPolicyState*        state,
                                  const RestartPolicyConfig* cfg);

// Sleeps for the current backoff duration, then doubles it (capped at max).
// Also increments restart_count.
void restart_policy_apply_backoff(RestartPolicyState*        state,
                                  const RestartPolicyConfig* cfg);

#endif // RESTART_POLICY_H
