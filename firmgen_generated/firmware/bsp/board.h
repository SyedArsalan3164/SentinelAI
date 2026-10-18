/* Declarative ESP32 carrier facts shared by vendor-neutral services. */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

#ifndef BOARD_NAME
#define BOARD_NAME "ESP32-WROOM-32 carrier; LED/button polarity unknown"
#endif

/* GPIO assignments are supplied in app_config.h; carrier polarity is not. */
const char *board_name(void);

#ifdef BOARD_LED_ACTIVE_LEVEL
static inline uint32_t board_led_level(bool is_on)
{
    return is_on ? (uint32_t)BOARD_LED_ACTIVE_LEVEL
                 : (uint32_t)!BOARD_LED_ACTIVE_LEVEL;
}
#endif

#ifdef BOARD_BUTTON_ACTIVE_LEVEL
static inline bool board_button_is_pressed(uint32_t pin_level)
{
    return pin_level == (uint32_t)BOARD_BUTTON_ACTIVE_LEVEL;
}
#endif

#endif /* BOARD_H */
