#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Application hardware assignments and bounded protocol/timing settings. */
#define APP_STROBE_GPIO                 (4U)
#define APP_BUZZER_GPIO                 (18U)
#define APP_SAFETY_RELAY_GPIO           (19U)
#define APP_LED_GPIO                    (2U)
#define APP_BUTTON_GPIO                 (0U)
#define APP_UART_PORT_NUMBER            (0U)
#define APP_UART_TX_GPIO                (1U)
#define APP_UART_RX_GPIO                (3U)
#define APP_UART_BAUD_RATE              (115200U)
#define APP_UART_LINE_MAX               (256U)
#define APP_UART_RX_BUFFER_SIZE         (512U)
#define APP_UART_TX_BUFFER_SIZE         (512U)
#define APP_UART_READ_TIMEOUT_MS        (10U)
#define APP_UART_READ_CHUNK_SIZE        (64U)
#define APP_TELEMETRY_PERIOD_MS         (2000U)
#define APP_STROBE_TOGGLE_PERIOD_MS     (200U)
#define APP_STATUS_LED_HALF_PERIOD_MS   (500U)
#define APP_BUTTON_DEBOUNCE_MS          (30U)
#define APP_BUTTON_SAMPLE_PERIOD_MS     (10U)
#define APP_CONTROL_TICK_MS             (10U)
#define APP_BUZZER_FREQUENCY_HZ         (2000U)
#define APP_BUZZER_DUTY_ON              (512U)
#define APP_EVENT_QUEUE_LENGTH          (16U)
#define APP_CONTROL_TASK_STACK_SIZE     (4096U)
#define APP_UART_TASK_STACK_SIZE        (4096U)
#define APP_TELEMETRY_TASK_STACK_SIZE   (3072U)
#define APP_CONTROL_TASK_PRIORITY       (20U)
#define APP_UART_TASK_PRIORITY          (19U)
#define APP_TELEMETRY_TASK_PRIORITY     (18U)

/*
 * Carrier wiring facts are deliberately not guessed:
 * define BOARD_BUTTON_ACTIVE_LEVEL in bsp/board.h from verified button wiring,
 * and BOARD_LED_ACTIVE_LEVEL there from carrier LED documentation or measurement.
 */

#endif /* APP_CONFIG_H */
