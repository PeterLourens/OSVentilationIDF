/*
    network_manager.h
    Unified network manager for WiFi + Ethernet

    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

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

#include "globals.h" // assumes sm_actuals, config structs, mutexes, etc. are declared here
#include "network_functions.h"

// Public API

// Start the network manager task (call once at startup).
bool network_manager_start(void);

// Notify the manager that network configuration changed (safe to call from ISR/context).
void network_manager_notify_config_change(void);

// Request a synchronous refresh of network actuals (non-blocking; schedules update).
void network_manager_request_actuals_update(void);

// Query whether manager is running
bool network_manager_is_running(void);

#endif // NETWORK_MANAGER_H
