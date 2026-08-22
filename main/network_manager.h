/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file network_manager.h
 * @brief Public interface for the network manager and Ethernet watchdog control.
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#define ETH_WD_PERIOD_MS 5000             // watchdog tick interval
#define ETH_WD_NO_IP_TIMEOUT_MS 60000     // link up but no IP (increased: DHCP needs time after restart)
#define ETH_WD_GATEWAY_FAILS_MAX 6        // 6 × 5s = 30s
#define ETH_WD_FORCE_RESTART_DELAY_MS 500 // delay between stop/start (increased from 50ms)

// Thresholds
#define ETH_WD_NO_ACTIVITY_TIMEOUT_MS 45000 // 45s without any successful activity -> restart (increased from 30s)
#define ETH_WD_ACTIVITY_GRACE_MS 5000       // 5s grace after events before considering stale
#define ETH_WD_RESTART_GRACE_MS 120000      // 120s grace period after restart before aggressive timeouts (new)

#define SOFTWARE_RESTART_MAX_ATTEMPTS 5
#define SOFTWARE_RESTART_DECAY_MS (60 * 1000) /* optional: decay counter after 60s */

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_eth.h>
#include <string.h>
#include <mqtt_client.h>
#include <mqtt5_client.h>

#include "globals.h"
#include "network_functions.h"
#include "mqtt.h"

// Request Home Assistant discovery to be performed (non-blocking; schedules discovery in MQTT task context).
void mqtt_request_homeassistant_discovery(void);

// Start the network manager task (call once at startup).
bool network_manager_start(void);

// Notify the manager that network configuration changed (safe to call from ISR/context).
void network_manager_notify_config_change(void);

// Request the network manager to apply the current MQTT configuration.
void network_manager_request_mqtt_restart(void);

// Request a synchronous refresh of network actuals (non-blocking; schedules update).
void network_manager_request_actuals_update(void);

// Query whether manager is running
bool network_manager_is_running(void);
void net_mgr_notify_eth_activity(void);

#endif // NETWORK_MANAGER_H
