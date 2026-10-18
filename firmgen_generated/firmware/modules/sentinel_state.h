/* Hardware-independent incident state transitions and counters. */
#ifndef SENTINEL_STATE_H
#define SENTINEL_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "sentinel_protocol.h"

typedef enum
{
    SENTINEL_STATE_IDLE = 0,
    SENTINEL_STATE_WARNING = 1,
    SENTINEL_STATE_CRITICAL = 2
} sentinel_state_id_t;

typedef struct
{
    sentinel_state_id_t state;
    uint32_t incidents;
    bool critical_latched;
} sentinel_state_t;

/**
 * @brief Initializes incident state to IDLE with a zero incident count.
 *
 * @param[out] state State storage to initialize.
 * @return true when state is non-NULL and initialized; otherwise false.
 * @pre state points to writable storage.
 * @post A successful call sets state to IDLE and incidents to zero.
 */
bool sentinel_state_init(sentinel_state_t *state);

/**
 * @brief Applies one accepted incident, preserving a latched critical state.
 *
 * Each accepted incident increments the count unless it has saturated at
 * UINT32_MAX. A critical incident latches CRITICAL; a warning cannot release
 * that latch.
 *
 * @param[in,out] state Incident state updated in place.
 * @param[in] event Accepted incident type.
 * @return true when state and/or incident count changed; otherwise false.
 * @pre state is initialized and event->type is supported.
 * @post Critical state remains latched until sentinel_state_reset() succeeds.
 */
bool sentinel_state_apply(sentinel_state_t *state,
                          const sentinel_event_t *event);

/**
 * @brief Resets incident state to IDLE without clearing the incident count.
 *
 * @param[in,out] state Incident state to reset.
 * @return true when state is non-NULL and reset; otherwise false.
 * @pre state is initialized.
 * @post state is IDLE, the critical latch is clear, and incidents is retained.
 */
bool sentinel_state_reset(sentinel_state_t *state);

#endif /* SENTINEL_STATE_H */
