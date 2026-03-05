#include "restart_policy.h"

#include "logger/logger.h"

#include <unistd.h>

void restart_policy_init(RestartPolicyState*        state,
                         const RestartPolicyConfig* cfg) {
    state->restart_count      = 0;
    state->window_start       = time(NULL);
    state->current_backoff_ms = cfg->initial_backoff_ms;
}

int restart_policy_should_restart(RestartPolicyState*        state,
                                  const RestartPolicyConfig* cfg) {
    time_t now = time(NULL);

    // Reset the window if enough time has passed since the last crash burst.
    if ((now - state->window_start) > cfg->restart_window_sec) {
        state->restart_count      = 0;
        state->window_start       = now;
        state->current_backoff_ms = cfg->initial_backoff_ms;
    }

    if (state->restart_count >= cfg->max_restarts) {
        LOG_WARN("RESTART", "Max restarts (%d) reached within %d seconds",
                 cfg->max_restarts, cfg->restart_window_sec);
        return 0;
    }

    return 1;
}

void restart_policy_apply_backoff(RestartPolicyState*        state,
                                  const RestartPolicyConfig* cfg) {
    LOG_INFO("RESTART", "Backoff %dms (attempt %d/%d)",
             state->current_backoff_ms, state->restart_count + 1,
             cfg->max_restarts);

    usleep((useconds_t)state->current_backoff_ms * 1000);

    state->current_backoff_ms *= 2;
    if (state->current_backoff_ms > cfg->max_backoff_ms) {
        state->current_backoff_ms = cfg->max_backoff_ms;
    }

    state->restart_count++;
}
