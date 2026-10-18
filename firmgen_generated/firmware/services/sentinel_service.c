/* Starts SentinelAI service behavior through a vendor-neutral platform seam. */
#include "sentinel_service.h"

#include "sentinel_platform_if.h"

/**
 * @brief Starts hardware-backed SentinelAI incident processing.
 * @return true when platform initialization succeeds; otherwise false.
 * @pre Called once during application startup.
 * @post Successful start configures safe output requests and launches services.
 */
bool sentinel_service_start(void)
{
    return sentinel_platform_start();
}
