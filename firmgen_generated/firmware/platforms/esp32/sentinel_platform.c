/* ESP-IDF adapter for SentinelAI GPIO, UART, NimBLE, and telemetry services. */
#include "sentinel_platform_if.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "store/config/ble_store_config.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sentinel_protocol.h"
#include "sentinel_state.h"

#define SENTINEL_GPIO_MASK(pin) (1ULL << (pin))
#define SENTINEL_STATE_CHARACTERISTIC_UUID (0x1001U)
#define SENTINEL_COUNT_CHARACTERISTIC_UUID (0x1002U)
#define SENTINEL_STATE_NAME "SentinelAI"

typedef struct
{
    uint32_t state;
    uint32_t incidents;
} sentinel_snapshot_t;

static const char *TAG = "sentinel_platform";
static const ble_uuid128_t sentinel_service_uuid =
    BLE_UUID128_INIT(0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
                     0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);
static const ble_uuid128_t sentinel_state_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00);
static const ble_uuid128_t sentinel_count_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x02, 0x10, 0x00, 0x00);

static sentinel_state_t incident_state;
static QueueHandle_t event_queue;
static SemaphoreHandle_t state_mutex;
static SemaphoreHandle_t uart_tx_mutex;
static uint16_t state_value_handle;
static uint16_t count_value_handle;
static uint8_t own_address_type;
static bool is_platform_started;
static bool is_actuator_task_ready;
static bool is_output_state_known;
static bool is_led_polarity_known;
static portMUX_TYPE ble_ready_lock = portMUX_INITIALIZER_UNLOCKED;
static bool ble_is_ready;
static bool ble_host_task_ready;
static bool is_button_reset_configured;

static void sentinel_apply_actuator_state(sentinel_state_id_t state);
static void sentinel_process_event(const sentinel_event_t *event);
static bool sentinel_snapshot(sentinel_snapshot_t *snapshot);
static void sentinel_reset_incident_state(void);
static int sentinel_gatt_access(uint16_t conn_handle,
                                uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *context,
                                void *argument);
static int sentinel_gap_event(struct ble_gap_event *event, void *argument);
static void sentinel_ble_advertise(void);
static void sentinel_ble_sync(void);
static void sentinel_ble_host_task(void *argument);
static void sentinel_uart_task(void *argument);
static void sentinel_control_task(void *argument);
static void sentinel_telemetry_task(void *argument);
static bool sentinel_uart_configure(void);
static bool sentinel_gpio_configure(void);
static bool sentinel_ledc_configure(void);
static bool sentinel_ble_configure(void);
static void sentinel_emit_telemetry(void);
static void sentinel_notify_changes(void);
static void sentinel_sample_reset_button(void);


static const struct ble_gatt_svc_def sentinel_gatt_services[] =
{
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &sentinel_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[])
        {
            {
                .uuid = &sentinel_state_uuid.u,
                .access_cb = sentinel_gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &state_value_handle
            },
            {
                .uuid = &sentinel_count_uuid.u,
                .access_cb = sentinel_gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &count_value_handle
            },
            { 0 }
        }
    },
    { 0 }
};

/**
 * @brief Sets an ESP32 GPIO output and reports driver failures.
 * @param[in] gpio Configured output pin.
 * @param[in] level Raw output level, zero or one.
 * @pre The pin has already been configured for output.
 * @post The requested level is applied or an error is logged.
 */
static void sentinel_set_gpio_level(gpio_num_t gpio, uint32_t level)
{
    esp_err_t error = gpio_set_level(gpio, (uint32_t)(level & 1U));

    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO %d output request failed: %s",
                 (int)gpio, esp_err_to_name(error));
    }
}

/**
 * @brief Converts logical LED state to its documented carrier polarity.
 * @param[in] is_on Requested logical LED state.
 * @param[out] level Raw output level for GPIO2.
 * @return true when carrier LED polarity is defined.
 * @pre level points to writable storage.
 * @post level is written only when BOARD_LED_ACTIVE_LEVEL is defined.
 */
static bool sentinel_get_led_level(bool is_on, uint32_t *level)
{
#ifdef BOARD_LED_ACTIVE_LEVEL
    if (level == NULL)
    {
        return false;
    }

    *level = board_led_level(is_on);
    return true;
#else
    (void)is_on;
    (void)level;
    return false;
#endif
}

/**
 * @brief Applies critical, warning, or idle actuator requests.
 *
 * Critical mode pulls relay GPIO19 LOW before strobe and buzzer activation.
 * With a 200 ms transition interval the GPIO4 square wave completes one cycle
 * every 400 ms, i.e. 2.5 Hz; this is the literal interpretation of five
 * transitions per second requested for the strobe.
 *
 * @param[in] state Logical incident state.
 * @pre GPIO and LEDC have been configured.
 * @post Relay, strobe, buzzer, and the optionally known-polarity LED are set.
 */
static void sentinel_apply_actuator_state(sentinel_state_id_t state)
{
    static bool strobe_is_high;
    static bool warning_led_is_on;
    uint32_t led_level;
    bool led_polarity_is_known = false;

    if (state == SENTINEL_STATE_CRITICAL)
    {
        sentinel_set_gpio_level((gpio_num_t)APP_SAFETY_RELAY_GPIO, 0U);
        (void)ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                            APP_BUZZER_DUTY_ON);
        (void)ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        strobe_is_high = !strobe_is_high;
        sentinel_set_gpio_level((gpio_num_t)APP_STROBE_GPIO,
                                strobe_is_high ? 1U : 0U);
        led_polarity_is_known = is_led_polarity_known &&
                                sentinel_get_led_level(false, &led_level);
        if (led_polarity_is_known)
        {
            sentinel_set_gpio_level((gpio_num_t)APP_LED_GPIO, led_level);
        }
        warning_led_is_on = false;
    }
    else if (state == SENTINEL_STATE_WARNING)
    {
        sentinel_set_gpio_level((gpio_num_t)APP_SAFETY_RELAY_GPIO, 1U);
        sentinel_set_gpio_level((gpio_num_t)APP_STROBE_GPIO, 0U);
        (void)ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0U);
        (void)ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        warning_led_is_on = !warning_led_is_on;
        led_polarity_is_known = is_led_polarity_known &&
                                sentinel_get_led_level(warning_led_is_on,
                                                       &led_level);
        if (led_polarity_is_known)
        {
            sentinel_set_gpio_level((gpio_num_t)APP_LED_GPIO, led_level);
        }
        strobe_is_high = false;
    }
    else
    {
        sentinel_set_gpio_level((gpio_num_t)APP_SAFETY_RELAY_GPIO, 1U);
        sentinel_set_gpio_level((gpio_num_t)APP_STROBE_GPIO, 0U);
        (void)ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0U);
        (void)ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        led_polarity_is_known = is_led_polarity_known &&
                                sentinel_get_led_level(false, &led_level);
        if (led_polarity_is_known)
        {
            sentinel_set_gpio_level((gpio_num_t)APP_LED_GPIO, led_level);
        }
        strobe_is_high = false;
        warning_led_is_on = false;
    }
}

/**
 * @brief Applies one accepted UART event and immediately services its outputs.
 * @param[in] event Validated incident from event_queue.
 * @pre event is non-NULL; state mutex is initialized.
 * @post State/count are committed before actuator outputs are driven.
 */
static void sentinel_process_event(const sentinel_event_t *event)
{
    sentinel_state_id_t new_state;
    bool is_changed;

    if ((event == NULL) || (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE))
    {
        return;
    }

    is_changed = sentinel_state_apply(&incident_state, event);
    new_state = incident_state.state;
    (void)xSemaphoreGive(state_mutex);

    if (is_changed)
    {
        if (is_actuator_task_ready)
        {
            sentinel_apply_actuator_state(new_state);
            is_output_state_known = true;
        }
        sentinel_notify_changes();
    }
}

/**
 * @brief Copies state and count under one mutex acquisition.
 * @param[out] snapshot Storage for consistent state and count values.
 * @return true when the snapshot is copied successfully.
 * @pre state_mutex is initialized.
 * @post snapshot fields describe the same critical section view.
 */
static bool sentinel_snapshot(sentinel_snapshot_t *snapshot)
{
    bool is_copied = false;

    if ((snapshot != NULL) &&
        (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE))
    {
        snapshot->state = (uint32_t)incident_state.state;
        snapshot->incidents = incident_state.incidents;
        (void)xSemaphoreGive(state_mutex);
        is_copied = true;
    }

    return is_copied;
}

/**
 * @brief Resets application state and promptly requests safe actuator levels.
 * @pre state_mutex and hardware outputs have been initialized.
 * @post State becomes IDLE, relay is HIGH, and strobe/buzzer are off.
 */
static void sentinel_reset_incident_state(void)
{
    sentinel_state_id_t reset_state = SENTINEL_STATE_IDLE;

    if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE)
    {
        (void)sentinel_state_reset(&incident_state);
        reset_state = incident_state.state;
        (void)xSemaphoreGive(state_mutex);
    }

    if (is_actuator_task_ready)
    {
        sentinel_apply_actuator_state(reset_state);
        is_output_state_known = true;
    }
    else
    {
        is_output_state_known = false;
    }
    sentinel_notify_changes();
}

/**
 * @brief Serves read requests for state and incident count values.
 * @param[in] conn_handle NimBLE connection requesting a value.
 * @param[in] attr_handle GATT value handle requested.
 * @param[in,out] context NimBLE read context; output mbuf is appended.
 * @param[in] argument Unused callback context.
 * @return 0 on success or a NimBLE ATT error code on failure.
 * @pre GATT service and shared-state mutex have been initialized.
 * @post Successful reads append a 4-byte little-endian integer.
 */
static int sentinel_gatt_access(uint16_t conn_handle,
                                uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *context,
                                void *argument)
{
    sentinel_snapshot_t snapshot;
    uint32_t value;
    uint8_t encoded_value[sizeof(uint32_t)];
    int result = BLE_ATT_ERR_UNLIKELY;

    (void)conn_handle;
    (void)argument;
    if ((context == NULL) ||
        (context->op != BLE_GATT_ACCESS_OP_READ_CHR) ||
        !sentinel_snapshot(&snapshot))
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (attr_handle == state_value_handle)
    {
        value = snapshot.state;
    }
    else if (attr_handle == count_value_handle)
    {
        value = snapshot.incidents;
    }
    else
    {
        return BLE_ATT_ERR_INVALID_HANDLE;
    }

    encoded_value[0] = (uint8_t)(value & 0xFFU);
    encoded_value[1] = (uint8_t)((value >> 8U) & 0xFFU);
    encoded_value[2] = (uint8_t)((value >> 16U) & 0xFFU);
    encoded_value[3] = (uint8_t)((value >> 24U) & 0xFFU);
    if (os_mbuf_append(context->om, encoded_value, sizeof(encoded_value)) == 0)
    {
        result = 0;
    }

    return result;
}

/**
 * @brief Begins connectable BLE advertising with the SentinelAI UUID.
 * @pre NimBLE has synchronized and the GATT service is registered.
 * @post Advertising is active or a specific failure is logged.
 */
static void sentinel_ble_advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_gap_adv_params parameters;
    int result;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&sentinel_service_uuid;
    fields.num_uuids128 = 1U;
    fields.uuids128_is_complete = 1U;
    fields.name = (uint8_t *)SENTINEL_STATE_NAME;
    fields.name_len = (uint8_t)(sizeof(SENTINEL_STATE_NAME) - 1U);
    fields.name_is_complete = 1U;

    result = ble_gap_adv_set_fields(&fields);
    if (result != 0)
    {
        ESP_LOGE(TAG, "BLE advertising data setup failed: %d", result);
        return;
    }

    memset(&parameters, 0, sizeof(parameters));
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    result = ble_gap_adv_start(own_address_type,
                               NULL,
                               BLE_HS_FOREVER,
                               &parameters,
                               sentinel_gap_event,
                               NULL);
    if (result != 0)
    {
        ESP_LOGE(TAG, "BLE advertising start failed: %d", result);
    }
}

/**
 * @brief Restarts BLE advertising after a failed connection or disconnect.
 * @param[in] event NimBLE GAP event.
 * @param[in] argument Unused application callback context.
 * @return 0 after event handling.
 * @pre NimBLE host is active.
 * @post Connectable advertising is restarted when appropriate.
 */
static int sentinel_gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    if (event == NULL)
    {
        return 0;
    }

    if (((event->type == BLE_GAP_EVENT_CONNECT) &&
         (event->connect.status != 0)) ||
        (event->type == BLE_GAP_EVENT_DISCONNECT))
    {
        sentinel_ble_advertise();
    }

    return 0;
}

/**
 * @brief Resolves the BLE address and advertises after NimBLE synchronization.
 * @pre Called by NimBLE after it initializes the host identity.
 * @post BLE state/count service is advertised or a failure is logged.
 */
static void sentinel_ble_sync(void)
{
    int result = ble_hs_util_ensure_addr(0U);

    if (result == 0)
    {
        result = ble_hs_id_infer_auto(0U, &own_address_type);
    }

    if (result == 0)
    {
        bool is_host_task_ready;
        portENTER_CRITICAL(&ble_ready_lock);
        ble_is_ready = true;
        is_host_task_ready = ble_host_task_ready;
        portEXIT_CRITICAL(&ble_ready_lock);
        if (is_host_task_ready)
        {
            sentinel_ble_advertise();
        }
    }
    else
    {
        ESP_LOGE(TAG, "BLE address synchronization failed: %d", result);
    }
}

/**
 * @brief Executes the NimBLE host event loop on its FreeRTOS host task.
 * @param[in] argument Unused task argument.
 * @pre Started through nimble_port_freertos_init().
 * @post Host task remains in NimBLE until stack shutdown.
 */
static void sentinel_ble_host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * @brief Receives bounded UART lines and queues only valid incident events.
 *
 * Oversized frames are dropped through newline, preventing a suffix from being
 * misinterpreted as a new JSON object. Parser errors do not emit UART logs.
 *
 * @param[in] argument Unused task argument.
 * @pre UART0 driver and event queue are ready.
 * @post Each accepted complete frame is placed on the bounded event queue.
 */
static void sentinel_uart_task(void *argument)
{
    uint8_t received_bytes[APP_UART_READ_CHUNK_SIZE];
    char line[APP_UART_LINE_MAX + 1U];
    size_t line_length = 0U;
    bool discard_line = false;

    (void)argument;
    for (;;)
    {
        int bytes_read = uart_read_bytes((uart_port_t)APP_UART_PORT_NUMBER,
                                         received_bytes,
                                         sizeof(received_bytes),
                                         pdMS_TO_TICKS(APP_UART_READ_TIMEOUT_MS));
        int byte_index;

        for (byte_index = 0; byte_index < bytes_read; byte_index++)
        {
            char character = (char)received_bytes[byte_index];
            if (character == '\n')
            {
                if (!discard_line)
                {
                    sentinel_event_t event;
                    size_t frame_length = line_length;

                    if ((frame_length > 0U) && (line[frame_length - 1U] == '\r'))
                    {
                        frame_length--;
                    }
                    if (sentinel_protocol_parse(line, frame_length, &event))
                    {
                        (void)xQueueSend(event_queue, &event, 0U);
                    }
                }

                line_length = 0U;
                discard_line = false;
            }
            else if (!discard_line)
            {
                if (line_length < APP_UART_LINE_MAX)
                {
                    line[line_length++] = character;
                }
                else
                {
                    discard_line = true;
                    line_length = 0U;
                }
            }
        }
    }
}

/**
 * @brief Tracks state outputs, transition timing, and optional reset button.
 *
 * Event queue has higher priority than heartbeat. GPIO0 reset is enabled only
 * if BOARD_BUTTON_ACTIVE_LEVEL is supplied from verified carrier documentation;
 * its input is externally biased, since no pull assumption is made.
 *
 * @param[in] argument Unused task argument.
 * @pre GPIO/PWM, queue, mutexes, and UART services are initialized.
 * @post Accepted critical events reach relay GPIO19 without BLE/UART blocking.
 */
static void sentinel_control_task(void *argument)
{
    sentinel_event_t event;
    TickType_t last_strobe_transition = xTaskGetTickCount();
    TickType_t last_warning_transition = xTaskGetTickCount();
    const TickType_t control_period = pdMS_TO_TICKS(APP_CONTROL_TICK_MS);
    const TickType_t reset_sample_period =
        pdMS_TO_TICKS(APP_BUTTON_SAMPLE_PERIOD_MS);
    TickType_t last_button_sample = xTaskGetTickCount();

    (void)argument;
    for (;;)
    {
        sentinel_snapshot_t snapshot;
        TickType_t current_tick;

        if (xQueueReceive(event_queue, &event, control_period) == pdTRUE)
        {
            sentinel_process_event(&event);
        }

        current_tick = xTaskGetTickCount();
        if (sentinel_snapshot(&snapshot))
        {
            sentinel_state_id_t current_state =
                (sentinel_state_id_t)snapshot.state;

            if (current_state == SENTINEL_STATE_CRITICAL)
            {
                if (!is_output_state_known ||
                    ((current_tick - last_strobe_transition) >=
                     pdMS_TO_TICKS(APP_STROBE_TOGGLE_PERIOD_MS)))
                {
                    sentinel_apply_actuator_state(current_state);
                    is_output_state_known = true;
                    last_strobe_transition = current_tick;
                }
            }
            else if (current_state == SENTINEL_STATE_WARNING)
            {
                if (!is_output_state_known ||
                    ((current_tick - last_warning_transition) >=
                     pdMS_TO_TICKS(APP_STATUS_LED_HALF_PERIOD_MS)))
                {
                    sentinel_apply_actuator_state(current_state);
                    is_output_state_known = true;
                    last_warning_transition = current_tick;
                }
            }
            else if (!is_output_state_known)
            {
                sentinel_apply_actuator_state(current_state);
                is_output_state_known = true;
            }
        }

        if (is_button_reset_configured &&
            ((current_tick - last_button_sample) >= reset_sample_period))
        {
            sentinel_sample_reset_button();
            last_button_sample = current_tick;
        }
    }
}

/**
 * @brief Debounces an externally biased reset input without assuming polarity.
 *
 * A confirmed transition to the documented active level resets the application
 * once. A release must be stable for the same interval before another press
 * can be recognized.
 *
 * @pre GPIO0 is configured as input and BOARD_BUTTON_ACTIVE_LEVEL is verified.
 * @post A stable active press resets state; unstable edges are ignored.
 */
static void sentinel_sample_reset_button(void)
{
#ifdef BOARD_BUTTON_ACTIVE_LEVEL
    static bool is_button_pressed;
    static uint32_t candidate_samples;
    static bool candidate_level;
    static bool is_debounce_initialized;
    int sampled_pin_level;
    uint32_t pin_level;
    bool sampled_pressed;
    uint32_t required_samples = APP_BUTTON_DEBOUNCE_MS /
                                SENTINEL_BUTTON_SAMPLE_PERIOD_MS;

    sampled_pin_level = gpio_get_level((gpio_num_t)APP_BUTTON_GPIO);
    if (sampled_pin_level < 0)
    {
        return;
    }

    pin_level = (uint32_t)sampled_pin_level;
    sampled_pressed = board_button_is_pressed(pin_level);
    if (!is_debounce_initialized)
    {
        candidate_level = sampled_pressed;
        is_button_pressed = sampled_pressed;
        candidate_samples = 1U;
        is_debounce_initialized = true;
        return;
    }

    if (sampled_pressed != candidate_level)
    {
        candidate_level = sampled_pressed;
        candidate_samples = 1U;
        return;
    }

    if (candidate_samples < required_samples)
    {
        candidate_samples++;
    }

    if ((candidate_samples >= required_samples) &&
        (sampled_pressed != is_button_pressed))
    {
        is_button_pressed = sampled_pressed;
        if (is_button_pressed)
        {
            sentinel_reset_incident_state();
        }
    }
#endif
}

/**
 * @brief Transmits one JSON heartbeat line every configured interval.
 * @param[in] argument Unused task argument.
 * @pre UART0 and state snapshot mutex are initialized.
 * @post One state/count/free-heap JSON line is sent every 2000 ms.
 */
static void sentinel_telemetry_task(void *argument)
{
    (void)argument;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(APP_TELEMETRY_PERIOD_MS));
        sentinel_emit_telemetry();
    }
}

/**
 * @brief Formats a fixed-size telemetry JSON line and writes it to UART0.
 * @pre uart_tx_mutex and UART0 driver exist.
 * @post A complete newline-terminated line is written or its failure logged.
 */
static void sentinel_emit_telemetry(void)
{
    sentinel_snapshot_t snapshot;
    uint32_t free_heap;
    char line[96U];
    int line_length;

    if (!sentinel_snapshot(&snapshot))
    {
        return;
    }

    free_heap = esp_get_free_heap_size();
    line_length = snprintf(line,
                           sizeof(line),
                           "{\"telemetry\":{\"state\":%lu,\"incidents\":%lu,\"heap\":%lu}}\n",
                           (unsigned long)snapshot.state,
                           (unsigned long)snapshot.incidents,
                           (unsigned long)free_heap);
    if ((line_length <= 0) || ((size_t)line_length >= sizeof(line)))
    {
        ESP_LOGE(TAG, "Telemetry JSON exceeded the fixed buffer");
        return;
    }

    if (xSemaphoreTake(uart_tx_mutex, portMAX_DELAY) == pdTRUE)
    {
        int bytes_written = uart_write_bytes((uart_port_t)APP_UART_PORT_NUMBER,
                                             line,
                                             (size_t)line_length);
        (void)xSemaphoreGive(uart_tx_mutex);
        if (bytes_written != line_length)
        {
            ESP_LOGE(TAG, "UART telemetry write incomplete: %d/%d",
                     bytes_written, line_length);
        }
    }
}

/**
 * @brief Signals NimBLE that state and incident count values have changed.
 * @pre GATT value handles have been registered.
 * @post NimBLE schedules notifications for peers that subscribed to either.
 */
static void sentinel_notify_changes(void)
{
    bool is_ready;

    portENTER_CRITICAL(&ble_ready_lock);
    is_ready = ble_is_ready && ble_host_task_ready;
    portEXIT_CRITICAL(&ble_ready_lock);

    if (is_ready)
    {
        (void)ble_gatts_chr_updated(state_value_handle);
        (void)ble_gatts_chr_updated(count_value_handle);
    }
}

/**
 * @brief Installs UART0 at 115200 8N1 on the documented UART0 GPIO pair.
 * @return true when driver, framing, and pin routing are configured.
 * @pre SDK application console is disabled on UART0.
 * @post UART0 receives on GPIO3 and transmits on GPIO1.
 */
static bool sentinel_uart_configure(void)
{
    const uart_config_t configuration =
    {
        .baud_rate = (int)APP_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    esp_err_t error = uart_driver_install((uart_port_t)APP_UART_PORT_NUMBER,
                                          APP_UART_RX_BUFFER_SIZE,
                                          APP_UART_TX_BUFFER_SIZE,
                                          0U,
                                          NULL,
                                          0U);

    if (error == ESP_OK)
    {
        error = uart_param_config((uart_port_t)APP_UART_PORT_NUMBER,
                                  &configuration);
    }
    if (error == ESP_OK)
    {
        error = uart_set_pin((uart_port_t)APP_UART_PORT_NUMBER,
                             (int)APP_UART_TX_GPIO,
                             (int)APP_UART_RX_GPIO,
                             UART_PIN_NO_CHANGE,
                             UART_PIN_NO_CHANGE);
    }
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "UART0 setup failed: %s", esp_err_to_name(error));
    }

    return error == ESP_OK;
}

/**
 * @brief Configures actuator outputs and known-safe application startup levels.
 * @return true when all output modes and initial levels are accepted.
 * @pre GPIO19 is active-low as specified; carrier reset/boot behavior remains separate.
 * @post Relay is requested HIGH; strobe and buzzer are off; LED is untouched if polarity unknown.
 */
static bool sentinel_gpio_configure(void)
{
    uint64_t output_mask = SENTINEL_GPIO_MASK(APP_STROBE_GPIO) |
                           SENTINEL_GPIO_MASK(APP_BUZZER_GPIO) |
                           SENTINEL_GPIO_MASK(APP_SAFETY_RELAY_GPIO);
    gpio_config_t configuration;
    esp_err_t error;

#ifdef BOARD_LED_ACTIVE_LEVEL
    output_mask |= SENTINEL_GPIO_MASK(APP_LED_GPIO);
#endif

    configuration.pin_bit_mask = output_mask;
    configuration.mode = GPIO_MODE_OUTPUT;
    configuration.pull_up_en = GPIO_PULLUP_DISABLE;
    configuration.pull_down_en = GPIO_PULLDOWN_DISABLE;
    configuration.intr_type = GPIO_INTR_DISABLE;
    error = gpio_config(&configuration);
    is_led_polarity_known = false;

    if (error == ESP_OK)
    {
        error = gpio_set_level((gpio_num_t)APP_SAFETY_RELAY_GPIO, 1U);
    }
    if (error == ESP_OK)
    {
        error = gpio_set_level((gpio_num_t)APP_STROBE_GPIO, 0U);
    }
    if (error == ESP_OK)
    {
        error = gpio_set_level((gpio_num_t)APP_BUZZER_GPIO, 0U);
    }
#ifdef BOARD_LED_ACTIVE_LEVEL
    if (error == ESP_OK)
    {
        uint32_t led_off_level = board_led_level(false);
        error = gpio_set_level((gpio_num_t)APP_LED_GPIO, led_off_level);
        is_led_polarity_known = (error == ESP_OK);
    }
#endif

    if (error == ESP_OK)
    {
#ifdef BOARD_BUTTON_ACTIVE_LEVEL
        const gpio_config_t button_configuration =
        {
            .pin_bit_mask = SENTINEL_GPIO_MASK(APP_BUTTON_GPIO),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        error = gpio_config(&button_configuration);
        is_button_reset_configured = (error == ESP_OK);
#else
        is_button_reset_configured = false;
#endif
    }

    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO initialization failed: %s", esp_err_to_name(error));
    }

    return error == ESP_OK;
}

/**
 * @brief Configures the GPIO18 buzzer PWM timer/channel in an off state.
 * @return true when LEDC timer and channel setup succeed.
 * @pre GPIO18 is output-capable and allocated for this service.
 * @post PWM tone source is configured at 2 kHz and initially silent.
 */
static bool sentinel_ledc_configure(void)
{
    const ledc_timer_config_t timer_configuration =
    {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = APP_BUZZER_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    const ledc_channel_config_t channel_configuration =
    {
        .gpio_num = (int)APP_BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0U,
        .hpoint = 0U
    };
    esp_err_t error = ledc_timer_config(&timer_configuration);

    if (error == ESP_OK)
    {
        error = ledc_channel_config(&channel_configuration);
    }
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "Buzzer PWM initialization failed: %s",
                 esp_err_to_name(error));
    }

    return error == ESP_OK;
}

/**
 * @brief Initializes NVS, NimBLE GATT database, then launches the BLE host.
 * @return true when NimBLE configuration and host task startup succeed.
 * @pre sdkconfig enables the ESP32 Bluetooth controller and NimBLE.
 * @post NimBLE advertises the registered SentinelAI service after sync.
 */
static bool sentinel_ble_configure(void)
{
    esp_err_t error = nvs_flash_init();
    int result;

    if ((error == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (error == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        error = nvs_flash_erase();
        if (error == ESP_OK)
        {
            error = nvs_flash_init();
        }
    }
    if (error == ESP_OK)
    {
        error = nimble_port_init();
    }
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "NimBLE or NVS initialization failed: %s",
                 esp_err_to_name(error));
        return false;
    }

    ble_hs_cfg.sync_cb = sentinel_ble_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    result = ble_svc_gap_device_name_set(SENTINEL_STATE_NAME);
    if (result == 0)
    {
        result = ble_gatts_count_cfg(sentinel_gatt_services);
    }
    if (result == 0)
    {
        result = ble_gatts_add_svcs(sentinel_gatt_services);
    }
    if (result != 0)
    {
        ESP_LOGE(TAG, "NimBLE GATT service registration failed: %d", result);
        return false;
    }

    nimble_port_freertos_init(sentinel_ble_host_task);
    portENTER_CRITICAL(&ble_ready_lock);
    ble_host_task_ready = true;
    portEXIT_CRITICAL(&ble_ready_lock);
    return true;
}


/**
 * @brief Starts the ESP32 adapter exactly once and creates its RTOS services.
 *
 * GPIO19 is requested HIGH before enabling input and BLE tasks. UART0 console is
 * disabled in Kconfig, but ROM bootloader output can still precede the app.
 *
 * @return true when all required resources and tasks are created successfully.
 * @pre Called once by sentinel_service_start().
 * @post IDLE application state is active; relay HIGH; strobe/buzzer off.
 */
bool sentinel_platform_start(void)
{
    if (is_platform_started)
    {
        return false;
    }
    is_platform_started = true;
    is_actuator_task_ready = false;
    is_output_state_known = false;

    if (!sentinel_state_init(&incident_state))
    {
        ESP_LOGE(TAG, "Incident state initialization failed");
        return false;
    }

    state_mutex = xSemaphoreCreateMutex();
    uart_tx_mutex = xSemaphoreCreateMutex();
    event_queue = xQueueCreate(APP_EVENT_QUEUE_LENGTH,
                               sizeof(sentinel_event_t));
    if ((state_mutex == NULL) || (uart_tx_mutex == NULL) ||
        (event_queue == NULL))
    {
        ESP_LOGE(TAG, "RTOS synchronization object creation failed");
        return false;
    }

    if (!sentinel_gpio_configure() ||
        !sentinel_ledc_configure() ||
        !sentinel_uart_configure() ||
        !sentinel_ble_configure())
    {
        return false;
    }

    if (xTaskCreate(sentinel_control_task, "sentinel_control",
                    APP_CONTROL_TASK_STACK_SIZE, NULL,
                    APP_CONTROL_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Safety control task creation failed");
        return false;
    }
    is_actuator_task_ready = true;

    if ((xTaskCreate(sentinel_uart_task, "sentinel_uart",
                     APP_UART_TASK_STACK_SIZE, NULL,
                     APP_UART_TASK_PRIORITY, NULL) != pdPASS) ||
        (xTaskCreate(sentinel_telemetry_task, "sentinel_telemetry",
                     APP_TELEMETRY_TASK_STACK_SIZE, NULL,
                     APP_TELEMETRY_TASK_PRIORITY, NULL) != pdPASS))
    {
        ESP_LOGE(TAG, "UART or telemetry task creation failed");
        sentinel_reset_incident_state();
        return false;
    }

    ESP_LOGI(TAG, "SentinelAI started; button and LED polarity require verified carrier facts");
    return true;
}
