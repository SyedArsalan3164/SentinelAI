/* Bounded parser for newline-delimited SentinelAI incident JSON. */
#include "sentinel_protocol.h"

#include <limits.h>
#include <string.h>

#define SENTINEL_JSON_STRING_MAX (32U)
#define SENTINEL_JSON_FIELD_EVENT (1U << 0U)
#define SENTINEL_JSON_FIELD_SEVERITY (1U << 1U)
#define SENTINEL_JSON_FIELD_ZONE (1U << 2U)
#define SENTINEL_JSON_REQUIRED_FIELDS (SENTINEL_JSON_FIELD_EVENT | SENTINEL_JSON_FIELD_SEVERITY | SENTINEL_JSON_FIELD_ZONE)

typedef struct
{
    const char *text;
    size_t length;
    size_t offset;
} sentinel_json_cursor_t;

/**
 * @brief Skips JSON whitespace from the current cursor position.
 * @param[in,out] cursor Bounded frame parser cursor.
 * @pre cursor references a valid frame buffer.
 * @post Only JSON whitespace is consumed.
 */
static void sentinel_json_skip_whitespace(sentinel_json_cursor_t *cursor)
{
    while ((cursor->offset < cursor->length) &&
           ((cursor->text[cursor->offset] == ' ') ||
            (cursor->text[cursor->offset] == '\t') ||
            (cursor->text[cursor->offset] == '\r') ||
            (cursor->text[cursor->offset] == '\n')))
    {
        cursor->offset++;
    }
}

/**
 * @brief Parses a JSON string into a bounded NUL-terminated destination.
 * @param[in,out] cursor Cursor positioned at an opening quote.
 * @param[out] destination Writable destination storage.
 * @param[in] destination_size Destination capacity, including NUL byte.
 * @return true when a complete string fits; otherwise false.
 * @pre cursor and destination are non-NULL and destination_size is nonzero.
 * @post Destination is valid only when true is returned.
 */
static bool sentinel_json_parse_string(sentinel_json_cursor_t *cursor,
                                       char *destination,
                                       size_t destination_size)
{
    size_t output_length = 0U;
    bool is_complete = false;

    if ((destination_size == 0U) || (cursor->offset >= cursor->length) ||
        (cursor->text[cursor->offset] != '"'))
    {
        return false;
    }

    cursor->offset++;
    while (cursor->offset < cursor->length)
    {
        char character = cursor->text[cursor->offset++];

        if (character == '"')
        {
            destination[output_length] = '\0';
            is_complete = true;
            break;
        }

        if (character == '\\')
        {
            if (cursor->offset >= cursor->length)
            {
                return false;
            }

            character = cursor->text[cursor->offset++];
            switch (character)
            {
                case '"':
                case '\\':
                case '/':
                    break;
                case 'b':
                    character = '\b';
                    break;
                case 'f':
                    character = '\f';
                    break;
                case 'n':
                    character = '\n';
                    break;
                case 'r':
                    character = '\r';
                    break;
                case 't':
                    character = '\t';
                    break;
                default:
                    return false;
            }
        }
        else if ((uint8_t)character < 0x20U)
        {
            return false;
        }

        if (output_length >= (destination_size - 1U))
        {
            return false;
        }
        destination[output_length++] = character;
    }

    return is_complete;
}

/**
 * @brief Parses an unsigned decimal zone identifier in uint32_t range.
 * @param[in,out] cursor Cursor positioned at the first number character.
 * @param[out] zone_id Destination for the parsed zone ID.
 * @return true when the value is a valid in-range JSON integer.
 * @pre cursor and zone_id are non-NULL.
 * @post zone_id changes only on success; leading zeros are rejected except 0.
 */
static bool sentinel_json_parse_zone(sentinel_json_cursor_t *cursor,
                                     uint32_t *zone_id)
{
    uint32_t parsed_value = 0U;
    size_t digit_count = 0U;
    bool has_leading_zero = false;

    while (cursor->offset < cursor->length)
    {
        char character = cursor->text[cursor->offset];
        uint32_t digit;

        if ((character < '0') || (character > '9'))
        {
            break;
        }

        digit = (uint32_t)(character - '0');
        if ((digit_count == 1U) && (parsed_value == 0U))
        {
            has_leading_zero = true;
        }
        if (parsed_value > ((UINT32_MAX - digit) / 10U))
        {
            return false;
        }

        parsed_value = (parsed_value * 10U) + digit;
        cursor->offset++;
        digit_count++;
    }

    if ((digit_count == 0U) || has_leading_zero)
    {
        return false;
    }

    *zone_id = parsed_value;
    return true;
}

/**
 * @brief Parses one bounded SentinelAI incident JSON object.
 *
 * The object must contain exactly the required event, severity, and zone_id
 * properties. Event must be ZONE_BREACH or NO_PPE; severity must be CRITICAL;
 * zone_id must be an unsigned 32-bit JSON integer. Unknown/duplicate
 * properties and trailing commas are rejected.
 *
 * @param[in] line Frame bytes borrowed for this call.
 * @param[in] line_length Frame size in bytes, excluding line ending.
 * @param[out] event Parsed event, updated only on success.
 * @return true when the complete object is valid and supported.
 * @return false for malformed JSON, unsupported values, or invalid arguments.
 * @pre line references at least line_length readable bytes.
 * @post event is unchanged when false is returned.
 * @note No I/O or dynamic memory allocation occurs.
 */
bool sentinel_protocol_parse(const char *line,
                             size_t line_length,
                             sentinel_event_t *event)
{
    sentinel_json_cursor_t cursor;
    sentinel_event_t parsed_event;
    uint8_t seen_fields = 0U;
    bool is_valid = true;
    bool expect_property = true;

    if ((line == NULL) || (event == NULL) || (line_length == 0U))
    {
        return false;
    }

    cursor.text = line;
    cursor.length = line_length;
    cursor.offset = 0U;
    parsed_event.type = 0;
    parsed_event.zone_id = 0U;

    sentinel_json_skip_whitespace(&cursor);
    if ((cursor.offset >= cursor.length) ||
        (cursor.text[cursor.offset++] != '{'))
    {
        return false;
    }

    sentinel_json_skip_whitespace(&cursor);
    if ((cursor.offset < cursor.length) &&
        (cursor.text[cursor.offset] == '}'))
    {
        return false;
    }

    while (is_valid && (cursor.offset < cursor.length))
    {
        char field_name[SENTINEL_JSON_STRING_MAX];
        char field_value[SENTINEL_JSON_STRING_MAX];
        uint8_t field_bit;

        if (!expect_property ||
            !sentinel_json_parse_string(&cursor,
                                        field_name,
                                        sizeof(field_name)))
        {
            is_valid = false;
            break;
        }

        sentinel_json_skip_whitespace(&cursor);
        if ((cursor.offset >= cursor.length) ||
            (cursor.text[cursor.offset++] != ':'))
        {
            is_valid = false;
            break;
        }
        sentinel_json_skip_whitespace(&cursor);

        if (strcmp(field_name, "event") == 0)
        {
            field_bit = SENTINEL_JSON_FIELD_EVENT;
            is_valid = sentinel_json_parse_string(&cursor,
                                                  field_value,
                                                  sizeof(field_value));
            if (is_valid && (strcmp(field_value, "ZONE_BREACH") == 0))
            {
                parsed_event.type = SENTINEL_EVENT_ZONE_BREACH;
            }
            else if (is_valid && (strcmp(field_value, "NO_PPE") == 0))
            {
                parsed_event.type = SENTINEL_EVENT_NO_PPE;
            }
            else
            {
                is_valid = false;
            }
        }
        else if (strcmp(field_name, "severity") == 0)
        {
            field_bit = SENTINEL_JSON_FIELD_SEVERITY;
            is_valid = sentinel_json_parse_string(&cursor,
                                                  field_value,
                                                  sizeof(field_value)) &&
                       (strcmp(field_value, "CRITICAL") == 0);
        }
        else if (strcmp(field_name, "zone_id") == 0)
        {
            field_bit = SENTINEL_JSON_FIELD_ZONE;
            is_valid = sentinel_json_parse_zone(&cursor,
                                                &parsed_event.zone_id);
        }
        else
        {
            is_valid = false;
            break;
        }

        if (!is_valid || ((seen_fields & field_bit) != 0U))
        {
            is_valid = false;
            break;
        }
        seen_fields = (uint8_t)(seen_fields | field_bit);

        sentinel_json_skip_whitespace(&cursor);
        if ((cursor.offset < cursor.length) &&
            (cursor.text[cursor.offset] == ','))
        {
            cursor.offset++;
            sentinel_json_skip_whitespace(&cursor);
            if ((cursor.offset >= cursor.length) ||
                (cursor.text[cursor.offset] == '}'))
            {
                is_valid = false;
            }
            expect_property = true;
        }
        else
        {
            expect_property = false;
            break;
        }
    }

    if (!is_valid || expect_property || (cursor.offset >= cursor.length) ||
        (cursor.text[cursor.offset++] != '}'))
    {
        return false;
    }

    sentinel_json_skip_whitespace(&cursor);
    is_valid = (cursor.offset == cursor.length) &&
               (seen_fields == SENTINEL_JSON_REQUIRED_FIELDS);
    if (is_valid)
    {
        *event = parsed_event;
    }

    return is_valid;
}
