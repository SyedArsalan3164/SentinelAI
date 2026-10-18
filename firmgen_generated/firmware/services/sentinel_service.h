/* SentinelAI application service startup contract. */
#ifndef SENTINEL_SERVICE_H
#define SENTINEL_SERVICE_H

#include <stdbool.h>

/**
 * @brief Starts the SentinelAI incident service and its platform adapter.
 *
 * @return true when the platform accepted startup; false on initialization
 *         failure.
 * @pre Called once by app_start().
 * @post On success incident input, BLE status, and telemetry are active.
 */
bool sentinel_service_start(void);

#endif /* SENTINEL_SERVICE_H */
