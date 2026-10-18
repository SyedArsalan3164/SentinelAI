/* Deterministic state transitions for safety incidents. */
#include "sentinel_state.h"

#include <limits.h>

/**
 * @brief Initializes the state model without touching hardware.
 * @param[out] state Writable state storage.
 * @return true when initialized; false for a NULL pointer.
 * @pre state points to writable storage.
 * @post Successful initialization establishes IDLE and a zero count.
 */
bool sentinel_state_init(sentinel_state_t *state)
{
    bool is_initialized = false;

    if (state != NULL)
    {
        state->state = SENTINEL_STATE_IDLE;
        state->incidents = 0U;
        state->critical_latched = false;
        is_initialized = true;
    }

    return is_initialized;
}

/**
 * @brief Records one valid incident and applies critical-latch precedence.
 * @param[in,out] state Initialized incident state.
 * @param[in] event Supported parsed incident.
 * @return true if a valid event changed state or count.
 * @pre state and event are non-NULL; event->type is supported.
 * @post Event count saturates at UINT32_MAX and critical remains latched.
 */
bool sentinel_state_apply(sentinel_state_t *state,
                          const sentinel_event_t *event)
{
    bool is_changed = false;

    if ((state == NULL) || (event == NULL))
    {
        return false;
    }

    if (state->incidents < UINT32_MAX)
    {
        state->incidents++;
        is_changed = true;
    }

    if (event->type == SENTINEL_EVENT_ZONE_BREACH)
    {
        state->critical_latched = true;
        state->state = SENTINEL_STATE_CRITICAL;
        is_changed = true;
    }
    else if ((event->type == SENTINEL_EVENT_NO_PPE) &&
             !state->critical_latched)
    {
        state->state = SENTINEL_STATE_WARNING;
        is_changed = true;
    }
    else
    {
        /* A warning during a latched critical incident cannot weaken state. */
    }

    return is_changed;
}

/**
 * @brief Clears active state and latch while preserving accumulated incidents.
 * @param[in,out] state Initialized incident state.
 * @return true when reset was applied; false for a NULL pointer.
 * @pre state is initialized.
 * @post State is IDLE and incident count is unchanged.
 */
bool sentinel_state_reset(sentinel_state_t *state)
{
    if (state == NULL)
    {
        return false;
    }

    state->state = SENTINEL_STATE_IDLE;
    state->critical_latched = false;
    return true;
}
