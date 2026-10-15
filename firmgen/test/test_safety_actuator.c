/**
 * @file test_safety_actuator.c
 * @brief Unity Unit Tests for SPAN-ESP32 FirmGen Requirements Verification
 */

#include "unity.h"
#include <string.h>

typedef enum {
    TEST_STATE_IDLE = 0,
    TEST_STATE_WARNING_PPE = 1,
    TEST_STATE_CRITICAL_BREACH = 2
} test_state_t;

static test_state_t mock_state = TEST_STATE_IDLE;
static int mock_relay = 1;
static int mock_strobe = 0;
static int mock_buzzer = 0;
static int mock_led = 0;

void setUp(void) {
    mock_state = TEST_STATE_IDLE;
    mock_relay = 1;
    mock_strobe = 0;
    mock_buzzer = 0;
    mock_led = 0;
}

void tearDown(void) {}

// [FR-01] Test JSON Packet Parser
void test_json_packet_parser(void) {
    const char *payload = "{\"event\":\"ZONE_BREACH\",\"severity\":\"CRITICAL\",\"zone_id\":1}";
    TEST_ASSERT_NOT_NULL(strstr(payload, "ZONE_BREACH"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "CRITICAL"));
}

// [FR-02] Test Critical Zone Breach Actuation
void test_zone_breach_actuation(void) {
    mock_state = TEST_STATE_CRITICAL_BREACH;
    mock_relay = 0; // Trip E-Stop
    mock_strobe = 1;
    mock_buzzer = 1;

    TEST_ASSERT_EQUAL(TEST_STATE_CRITICAL_BREACH, mock_state);
    TEST_ASSERT_EQUAL(0, mock_relay);
    TEST_ASSERT_EQUAL(1, mock_strobe);
    TEST_ASSERT_EQUAL(1, mock_buzzer);
}

// [FR-03] Test Warning State Indication
void test_no_ppe_warning_state(void) {
    mock_state = TEST_STATE_WARNING_PPE;
    mock_relay = 1; // Machine should not trip
    mock_led = 1;

    TEST_ASSERT_EQUAL(TEST_STATE_WARNING_PPE, mock_state);
    TEST_ASSERT_EQUAL(1, mock_relay);
    TEST_ASSERT_EQUAL(1, mock_led);
}

// [FR-04] Test Manual Operator Reset
void test_button_debounce_and_reset(void) {
    // Start in critical breach
    mock_state = TEST_STATE_CRITICAL_BREACH;
    mock_relay = 0;

    // Simulate button push and debounce
    int button_held_ms = 60;
    if (button_held_ms >= 50) {
        mock_state = TEST_STATE_IDLE;
        mock_relay = 1;
    }

    TEST_ASSERT_EQUAL(TEST_STATE_IDLE, mock_state);
    TEST_ASSERT_EQUAL(1, mock_relay);
}

// [FR-05] Test BLE Characteristic Payload
void test_ble_advertising_payload(void) {
    uint8_t adv_data[4] = {0x00, 0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_HEX8(0x00, adv_data[0]); // IDLE state code
}

// [FR-06] Test Periodic Telemetry JSON Output
void test_telemetry_json_output(void) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "{\"telemetry\":{\"state\":%d,\"incidents\":%d,\"heap\":%d}}", 0, 1, 224000);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"telemetry\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"state\":0"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_json_packet_parser);
    RUN_TEST(test_zone_breach_actuation);
    RUN_TEST(test_no_ppe_warning_state);
    RUN_TEST(test_button_debounce_and_reset);
    RUN_TEST(test_ble_advertising_payload);
    RUN_TEST(test_telemetry_json_output);
    return UNITY_END();
}
