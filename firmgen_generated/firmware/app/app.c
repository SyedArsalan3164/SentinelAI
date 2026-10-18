/* Top-level SentinelAI startup orchestration. */
#include "app.h"

#include "logger.h"
#include "sentinel_service.h"

static const char *TAG = "sentinel_app";

/**
 * @brief Starts the SentinelAI service after the ESP-IDF reset hook.
 *
 * @pre Called once by app_main().
 * @post The SentinelAI platform service runs, or a startup failure is logged.
 */
void app_start(void)
{
    if (!sentinel_service_start())
    {
        ESP_LOGE(TAG, "SentinelAI service startup failed");
    }
}
