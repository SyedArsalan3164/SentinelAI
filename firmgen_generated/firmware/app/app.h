/* Vendor-agnostic SentinelAI application entry point. */
#ifndef APP_H
#define APP_H

/**
 * @brief Starts top-level application services after the vendor reset hook.
 * @pre Called once from app_main().
 * @post Application services are running or startup failure is reported.
 */
void app_start(void);

#endif /* APP_H */
