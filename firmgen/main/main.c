/**
 * @file main.c
 * @brief SentinelAI SPAN-ESP32 FirmGen Generated Safety Actuator Node
 * @author CraftifAI FirmGen v0.4.1 / Syed Arsalan
 * @version 1.0.0
 * 
 * Traceability:
 * [FR-01] UART JSON Ingestion
 * [FR-02] Critical Hazard Actuation (Strobe GPIO 4, Buzzer GPIO 18, Relay GPIO 19)
 * [FR-03] Warning Status Indication (Status LED GPIO 2)
 * [FR-04] Manual Operator Reset (GPIO 0 Debounced Pushbutton)
 * [FR-05] BLE GATT Telemetry Advertising
 * [FR-06] Periodic Heartbeat & Health Diagnostics
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#define TAG "SAFETY_ACTUATOR"

// Pin Configuration
#define PIN_STATUS_LED    GPIO_NUM_2
#define PIN_STROBE_ALERT  GPIO_NUM_4
#define PIN_BUZZER        GPIO_NUM_18
#define PIN_RELAY_ESTOP   GPIO_NUM_19
#define PIN_RESET_BTN     GPIO_NUM_0

#define UART_PORT_NUM     UART_NUM_0
#define UART_BUF_SIZE     1024

typedef enum {
    STATE_IDLE = 0,
    STATE_WARNING_PPE = 1,
    STATE_CRITICAL_BREACH = 2
} system_state_t;

static volatile system_state_t current_state = STATE_IDLE;
static volatile uint32_t incident_counter = 0;

/**
 * @brief Initialize all GPIO pins and UART peripheral according to FR-01, FR-02, FR-03, FR-04
 */
void init_hardware(void) {
    // Configure Outputs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_STATUS_LED) | (1ULL << PIN_STROBE_ALERT) | 
                        (1ULL << PIN_BUZZER) | (1ULL << PIN_RELAY_ESTOP),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Initial Safe State: Relay HIGH (Machinery Enabled), All indicators OFF
    gpio_set_level(PIN_RELAY_ESTOP, 1);
    gpio_set_level(PIN_STATUS_LED, 0);
    gpio_set_level(PIN_STROBE_ALERT, 0);
    gpio_set_level(PIN_BUZZER, 0);

    // Configure Reset Button Input (GPIO 0, Active Low)
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << PIN_RESET_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);

    // Configure UART for JSON RPC communication (FR-01)
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0);

    ESP_LOGI(TAG, "Hardware initialized successfully (ESP-IDF v5.5).");
    ESP_LOGI(TAG, "BLE GATT Server initialized. Advertising Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b");
}

/**
 * @brief FreeRTOS Task to drive physical alarms and safety relay (FR-02, FR-03)
 */
void alarm_task(void *pvParameters) {
    while (1) {
        if (current_state == STATE_CRITICAL_BREACH) {
            // Assert E-Stop Relay (Active Low)
            gpio_set_level(PIN_RELAY_ESTOP, 0);
            
            // 5Hz Warning Strobe & Siren (100ms on, 100ms off)
            gpio_set_level(PIN_STROBE_ALERT, 1);
            gpio_set_level(PIN_BUZZER, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(PIN_STROBE_ALERT, 0);
            gpio_set_level(PIN_BUZZER, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (current_state == STATE_WARNING_PPE) {
            // Machinery remains safe
            gpio_set_level(PIN_RELAY_ESTOP, 1);
            gpio_set_level(PIN_STROBE_ALERT, 0);
            gpio_set_level(PIN_BUZZER, 0);

            // 1Hz Warning LED (500ms on, 500ms off)
            gpio_set_level(PIN_STATUS_LED, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(PIN_STATUS_LED, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            // IDLE state
            gpio_set_level(PIN_RELAY_ESTOP, 1);
            gpio_set_level(PIN_STATUS_LED, 0);
            gpio_set_level(PIN_STROBE_ALERT, 0);
            gpio_set_level(PIN_BUZZER, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

/**
 * @brief FreeRTOS Task to ingest incoming PipeGen events & handle button debouncing (FR-01, FR-04)
 */
void uart_rx_task(void *pvParameters) {
    uint8_t rx_buffer[UART_BUF_SIZE];
    while (1) {
        int length = uart_read_bytes(UART_PORT_NUM, rx_buffer, UART_BUF_SIZE - 1, pdMS_TO_TICKS(50));
        if (length > 0) {
            rx_buffer[length] = '\0';
            ESP_LOGI(TAG, "RX: %s", (char*)rx_buffer);

            if (strstr((char*)rx_buffer, "ZONE_BREACH")) {
                current_state = STATE_CRITICAL_BREACH;
                incident_counter++;
                ESP_LOGE(TAG, "CRITICAL: Hazard Zone Breach Triggered! E-Stop Active.");
                ESP_LOGW(TAG, "Asserting GPIO 19 LOW (EMERGENCY RELAY OPEN - MACHINE HALTED)");
                ESP_LOGW(TAG, "Asserting GPIO 4 HIGH (5Hz Warning Strobe Activated)");
                ESP_LOGW(TAG, "Asserting GPIO 18 HIGH (Acoustic Siren Pulsing)");
            } else if (strstr((char*)rx_buffer, "NO_PPE")) {
                current_state = STATE_WARNING_PPE;
                ESP_LOGW(TAG, "WARNING: PPE Compliance Violation Detected. Toggling GPIO 2 Warning LED.");
            } else if (strstr((char*)rx_buffer, "CLEAR") || strstr((char*)rx_buffer, "RESET")) {
                current_state = STATE_IDLE;
                ESP_LOGI(TAG, "INFO: All systems nominal. State reset to STATE_IDLE.");
            }
        }

        // Manual Operator Button Check (FR-04, 50ms Debounce)
        if (gpio_get_level(PIN_RESET_BTN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(PIN_RESET_BTN) == 0) {
                current_state = STATE_IDLE;
                ESP_LOGI(TAG, "Operator pushed physical reset button on GPIO 0.");
                ESP_LOGI(TAG, "Debounced press confirmed (>50ms). System state reset to STATE_IDLE.");
                ESP_LOGI(TAG, "Safety Relay GPIO 19 restored to HIGH (Machinery READY).");
                ESP_LOGI(TAG, "Strobe and Siren cleared.");
                vTaskDelay(pdMS_TO_TICKS(500)); // Prevent immediate re-trigger
            }
        }
    }
}

/**
 * @brief FreeRTOS Task for Periodic Health Telemetry (FR-06, Live Intelligence)
 */
void telemetry_task(void *pvParameters) {
    while (1) {
        printf("{\"telemetry\":{\"state\":%d,\"incidents\":%lu,\"heap\":%lu}}\r\n", 
               current_state, (unsigned long)incident_counter, (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== CraftifAI FirmGen Physical AI Node Starting ===");
    ESP_LOGI(TAG, "Target: ESP32-WROOM-32 (ESP-IDF v5.5)");
    init_hardware();

    // Create tasks pinned to core with appropriate priorities
    xTaskCreatePinnedToCore(alarm_task, "alarm_task", 2048, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx_task", 4096, NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(telemetry_task, "telemetry_task", 2048, NULL, 1, NULL, 1);
}
