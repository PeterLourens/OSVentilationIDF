/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file eth_functions.h
 * @brief Public Ethernet lifecycle and configuration API for the W5500 interface.
 *
 * This header exposes the Ethernet initialization, restart, teardown, and
 * configuration helpers used by the OSVentilation runtime. It also declares
 * helpers for link status, connection state, and shutdown coordination.
 */

#ifndef TASK_ETH_FUNCTIONS_H
#define TASK_ETH_FUNCTIONS_H

#include <driver/spi_master.h>
#include <esp_netif.h>
#include <esp_eth_netif_glue.h>
#include <esp_eth.h>
#include <esp_eth_mac.h>
#include <esp_eth_phy.h>
#include <esp_eth_mac_spi.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <arpa/inet.h>
#include <esp_idf_version.h>

#include "logging.h"
#include "globals.h"
#include "sys_stats.h"

extern esp_netif_t *eth_netif;
extern esp_eth_handle_t eth_handle;

/**
 * @brief Create the Ethernet lifecycle mutex once.
 *
 * This function is safe to call multiple times; it allocates the mutex on the
 * first invocation and no-ops on subsequent calls.
 */
void eth_lifecycle_init_once(void);

/**
 * @brief Perform a guarded hardware reset of the Ethernet controller.
 *
 * This function safely resets the W5500 and reinitializes the Ethernet stack,
 * ensuring that no lifecycle races occur during the reset.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t eth_hardware_reset(void);

/**
 * @brief Safely teardown Ethernet resources when the lifecycle mutex is available.
 *
 * If the lifecycle mutex cannot be obtained, teardown is deferred.
 */
void eth_teardown_safe(void);

/**
 * @brief Check whether Ethernet is connected and has a valid IP address.
 *
 * @return true if Ethernet is connected, false otherwise.
 */
bool eth_is_connected(void);

/**
 * @brief Check whether the Ethernet PHY/link appears to be up.
 *
 * This may query the PHY directly or fall back to checking the assigned IP.
 * @return true if the Ethernet link is up, false otherwise.
 */
bool ethernet_link_is_up(void);

/**
 * @brief Initialize the Ethernet interface and driver.
 *
 * This function is idempotent and protects the Ethernet lifecycle with a mutex.
 */
void eth_init(void);

/**
 * @brief Attempt a guarded Ethernet restart when the link is down.
 *
 * This helper applies backoff and may defer teardown if the lifecycle mutex is busy.
 */
void attempt_guarded_eth_restart(void);

/**
 * @brief Retry a previously deferred Ethernet teardown if required.
 *
 * Called periodically by the network manager task.
 */
void handle_deferred_eth_teardown_if_needed(void);

/**
 * @brief Start the Ethernet driver and begin network operation.
 *
 * If the driver is not initialized, it will be initialized first.
 */
void eth_start(void);

/**
 * @brief Restart the Ethernet driver in software without a full teardown.
 *
 * This stops and restarts the current Ethernet driver instance while holding the
 * lifecycle mutex.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t eth_software_restart(void);

/**
 * @brief Stop Ethernet operation and release driver resources.
 *
 * This function performs a full cleanup of Ethernet state.
 */
void eth_stop(void);

/**
 * @brief Check whether Ethernet is enabled in the runtime network configuration.
 *
 * @return true if Ethernet is configured as enabled.
 */
bool ethernet_config_is_enabled(void);

/**
 * @brief Check whether Ethernet DHCP is enabled in the runtime network configuration.
 *
 * @return true if DHCP is enabled for Ethernet.
 */
bool ethernet_config_is_dhcp_enabled(void);

/**
 * @brief Apply the current Ethernet configuration from the shared network config.
 *
 * This updates DHCP/static IP settings and hostname for the Ethernet interface.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t ethernet_config_apply(void);

/**
 * @brief Mark that Ethernet shutdown is in progress.
 *
 * @param v true if Ethernet is currently shutting down, false otherwise.
 */
void eth_set_shutting_down(bool v);

/**
 * @brief Query whether Ethernet shutdown is currently in progress.
 *
 * @return true if shutdown is active, false otherwise.
 */
bool eth_is_shutting_down(void);

/**
 * @brief Retrieve the current ESP Ethernet driver handle.
 *
 * @return The current esp_eth_handle_t, or NULL if not initialized.
 */
esp_eth_handle_t eth_get_handle(void);

#endif