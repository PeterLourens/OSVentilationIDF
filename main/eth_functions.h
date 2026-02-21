/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef TASK_ETH_FUNCTIONS_H
#define TASK_ETH_FUNCTIONS_H

#include <driver/spi_master.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <esp_eth_mac.h>
#include <esp_eth_phy.h>
#include <esp_eth_mac_spi.h>
#include <string.h>
#include <arpa/inet.h>

#include "logging.h"
#include "globals.h"
// #include "wifi_functions.h"

extern esp_netif_t *eth_netif;
extern esp_eth_handle_t eth_handle;

// Lifecycle
void eth_init(void);
void eth_start(void);
void eth_stop(void);

bool eth_is_connected(void);

// High level manager: initialize, register handlers, start driver, apply config,
// and perform boot-time fallback decisions (Wi‑Fi or maintenance AP).
void eth_manager_start(void);

// Apply current Ethernet configuration (reads globals::network_config).
esp_err_t ethernet_config_apply(void);

// Helpers used by other modules
bool ethernet_config_is_enabled(void);
bool ethernet_config_is_dhcp_enabled(void);
bool ethernet_and_wifi_both_disabled(void);

// Maintenance AP control (used internally and by task_eth if needed)
void start_maintenance_ap(void);
void stop_maintenance_ap(void);

// User override: force Wi‑Fi on/off regardless of config (task_eth calls this)
void eth_set_wifi_forced(bool forced);

#endif