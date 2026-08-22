/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef NETWORK_FUNCTIONS_H
#define NETWORK_FUNCTIONS_H

#include <esp_netif.h>
#include <sys/socket.h>

#include "eth_functions.h"

/**
 * @file network_functions.h
 * @brief Network utility and status functions for OSVentilation.
 */

typedef enum
{
    LED_OFF,
    LED_SLOW_BLINK,
    LED_FAST_BLINK,
    LED_DOUBLE_BLINK,
    LED_SOLID
} led_mode_t;

/**
 * @brief Request a syslog state update based on current network conditions.
 */
void update_syslog_state(void);

bool wifi_manager_ap_is_running(void);

/**
 * @brief Validate an IPv4 address string.
 *
 * @param[in] ip_address IPv4 address string.
 * @return true if the string contains a valid IPv4 address.
 */
bool is_valid_ip_address(const char *ip_address);

/**
 * @brief Check whether the Wi-Fi STA interface has an IPv4 address.
 *
 * @return true if Wi-Fi STA has a valid IP.
 */
bool wifi_has_ip(void);

/**
 * @brief Get the currently active network interface.
 *
 * @return Active ESP network interface handle, or NULL if none.
 */
esp_netif_t *get_active_netif(void);

/**
 * @brief Update the hostname field in runtime status from the active interface.
 */
void update_hostname(void);

/**
 * @brief Check whether any active network interface currently has an IP.
 *
 * @return true if the system has an active IP address.
 */
bool network_has_ip(void);

/**
 * @brief Handle the end of an ICMP ping measurement.
 *
 * @param hdl Ping handle.
 * @param args User-provided argument pointer.
 */
void on_ping_end(esp_ping_handle_t hdl, void *args);

/**
 * @brief Probe the gateway to verify network reachability.
 *
 * @return true if the gateway is reachable.
 */
bool ping_gateway(void);

// LED helpers
/**
 * @brief Update status LEDs for network connectivity.
 */
void update_network_led_state(void);

// control the Ethernet LED (GPIO45) to indicate link/activity
/**
 * @brief Initialize Ethernet LED control.
 */
void eth_led_init(void);

/**
 * @brief Set the Ethernet LED mode.
 *
 * @param mode Desired LED mode.
 */
void eth_led_set_mode(led_mode_t mode);

/**
 * @brief Set the Wi-Fi LED mode.
 *
 * @param mode Desired LED mode.
 */
void wifi_led_set_mode(led_mode_t mode);

/**
 * @brief Perform a periodic health check of network and Ethernet state.
 */
void network_manager_health_check(void);

#endif // NETWORK_FUNCTIONS_H