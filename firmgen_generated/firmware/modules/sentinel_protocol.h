/* SentinelAI UART frame parser contract; contains no hardware dependencies. */
#ifndef SENTINEL_PROTOCOL_H
#define SENTINEL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    SENTINEL_EVENT_ZONE_BREACH = 1,
    SENTINEL_EVENT_NO_PPE = 2
} sentinel_event_type_t;

typedef struct
{
    sentinel_event_type_t type;
    uint32_t zone_id;
} sentinel_event_t;

/**
 * @brief Parses one bounded SentinelAI incident JSON object.
 *
 * Accepts an object containing exactly the `event`, `severity`, and `zone_id`
 * fields. Severity must be `CRITICAL`; zone_id must be an unsigned 32-bit
 * integer. JSON whitespace and escaped ASCII string characters are accepted.
 *
 * @param[in] line Pointer to the frame bytes, borrowed for this call.
 * @param[in] line_length Number of bytes in line, excluding any line ending.
 * @param[out] event Receives the parsed event only on success.
 *
 * @return true when all required fields are valid and the object is complete.
 * @return false for malformed JSON, unsupported fields, invalid values, or
 *         invalid arguments.
 *
 * @pre line references at least line_length readable bytes.
 * @post event is unchanged when false is returned.
 * @note This function performs no I/O and allocates no memory.
 */
bool sentinel_protocol_parse(const char *line,
                             size_t line_length,
                             sentinel_event_t *event);

#endif /* SENTINEL_PROTOCOL_H */
