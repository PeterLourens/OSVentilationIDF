/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
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
#include <string.h>
#include <arpa/inet.h>

#include "logging.h"
#include "globals.h"
#include "sys_stats.h"

extern esp_netif_t *eth_netif;
extern esp_eth_handle_t eth_handle;

// Lifecycle
void eth_lifecycle_init_once(void);
void eth_lifecycle_init(void);
void eth_teardown_safe(void);
void eth_init(void);
void eth_start(void);
void eth_stop(void);

bool eth_is_connected(void);
bool ethernet_config_is_enabled(void);
bool ethernet_config_is_dhcp_enabled(void);

// Apply current Ethernet configuration (reads globals::network_config).
esp_err_t ethernet_config_apply(void);

#endif