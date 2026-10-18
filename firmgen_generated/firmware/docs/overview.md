# SentinelAI Safety Actuator Node

## Purpose and software behavior

The ESP-IDF 5.5 firmware receives newline-terminated incident JSON over UART0, applies a small incident state machine, drives the assigned actuator GPIOs, serves state and event count over NimBLE GATT, and sends heartbeat telemetry over UART0.

State values are IDLE=0, NO_PPE warning=1, and latched ZONE_BREACH critical=2. Each valid event increments a RAM-only incident count, saturating at `UINT32_MAX`. A NO_PPE warning cannot release the critical latch. A reset press (if configured after carrier wiring is verified) clears the active state to IDLE while retaining the event count.

## Configured signals and parameters

Values are centralized in `firmware/configs/app_config.h`:

| Function | GPIO / setting |
|---|---:|
| Strobe | GPIO4 |
| PWM buzzer | GPIO18, 2 kHz, 50% duty |
| Active-low safety relay | GPIO19; asserted LOW, requested HIGH when idle/warning |
| Status LED | GPIO2; behavior remains disabled until polarity is documented |
| Reset button | GPIO0; reset sampling remains disabled until active level is documented |
| UART0 TX / RX | GPIO1 / GPIO3, 115200 8N1 |
| Telemetry interval | 2000 ms |
| UART input line limit | 256 bytes |
| Button debounce | 30 ms, sampled at 10 ms |

The strobe timing interprets 5 Hz as five GPIO transitions per second: one transition every 200 ms, equivalent to a 2.5 Hz full on/off cycle. If the requirement means five full cycles per second, use a 100 ms transition interval instead.

## UART protocol

The receiver accepts complete JSON objects containing `event`, `severity`, and `zone_id`, for example `{"event":"ZONE_BREACH","severity":"CRITICAL","zone_id":1}`. Supported events are `ZONE_BREACH` and `NO_PPE`; severity must be `CRITICAL`; zone_id must be an unsigned 32-bit integer. CRLF is accepted, malformed frames are ignored, and overlong frames are discarded through their terminating newline.

Telemetry is written as one newline-terminated record every two seconds:

```json
{"telemetry":{"state":0,"incidents":0,"heap":123456}}
```

The SDK application console is disabled so UART0 can carry this protocol. The ROM bootloader may still print before application startup; clean UART behavior from power-on has not been measured.

## BLE GATT service

NimBLE advertises service UUID `4fafc201-1fb5-459e-8fcc-c5c9c331914b`. The state and incident count are readable/notifiable 32-bit little-endian values with characteristic UUIDs `00001001-0000-1000-8000-00805f9b34fb` and `00001002-0000-1000-8000-00805f9b34fb`.

## Hardware integration status

The code uses the ESP32 UART0 pin mapping from the session board context, but the external peer wiring, common ground, and electrical signal level remain unconfirmed. GPIO0 button pressed/released level and pull circuitry are unknown, so button reset is deliberately not enabled without `BOARD_BUTTON_ACTIVE_LEVEL`. GPIO2 active polarity is unknown, so the status LED is not driven without `BOARD_LED_ACTIVE_LEVEL`.

Application startup requests relay GPIO19 HIGH after GPIO configuration, but this does not guarantee a safe relay state during reset or ROM boot. The external relay circuit must be measured during reset and boot before connecting a safety-critical load.

No hardware test has been run: the critical response deadline (<20 ms), strobe/buzzer output, LED blink, reset debounce, BLE advertising/notifications, UART telemetry interval, and UART framing need target-board verification. No unit-test run is recorded.

## Build and documentation

Use the ESP-IDF project build workflow for the ESP32 target. The build tool reported success during this session, but it did not create a local build directory or compile database, so an independent syntax-only check could not be run. Doxygen HTML generation was attempted, but Doxygen is not installed in the environment.
