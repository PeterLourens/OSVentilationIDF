/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#define ETH_WD_PERIOD_MS 5000            // watchdog tick interval
#define ETH_WD_NO_IP_TIMEOUT_MS 30000    // link up but no IP
#define ETH_WD_GATEWAY_FAILS_MAX 6       // 6 × 5s = 30s
#define ETH_WD_FORCE_RESTART_DELAY_MS 50 // small delay between stop/start

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

// Request a synchronous refresh of network actuals (non-blocking; schedules update).
void network_manager_request_actuals_update(void);

// Query whether manager is running
bool network_manager_is_running(void);

#endif // NETWORK_MANAGER_H
