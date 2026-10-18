/* Contract implemented by the ESP32 SentinelAI hardware adapter. */
#ifndef SENTINEL_PLATFORM_IF_H
#define SENTINEL_PLATFORM_IF_H

#include <stdbool.h>

/**
 * @brief Starts the platform adapter after configuring safety outputs.
 *
 * The implementation configures actuator-safe software levels before starting
 * UART, BLE, and telemetry tasks. Physical reset/boot behavior is determined
 * by the carrier circuit and is not guaranteed by this API.
 *
 * @return true when platform initialization and required tasks start.
 * @return false when peripheral or RTOS initialization fails.
 * @pre Called once from application startup.
 * @post On success the ESP32 protocol and telemetry services are running.
 */
bool sentinel_platform_start(void);

#endif /* SENTINEL_PLATFORM_IF_H */
