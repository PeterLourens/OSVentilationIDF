/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef NETWORK_FUNCTIONS_H
#define NETWORK_FUNCTIONS_H

#include <esp_netif.h>
#include <sys/socket.h>

#include "eth_functions.h"

void update_syslog_state(void);
bool is_valid_ip_address(const char *ip_address);
bool wifi_has_ip(void);
esp_netif_t *get_active_netif(void);
void update_hostname(void);
bool network_has_ip(void);

void on_ping_end(esp_ping_handle_t hdl, void *args);
bool ping_gateway(void);

bool network_functions_get_interface_counters(const char *ifkey, uint64_t *rx_bytes, uint64_t *tx_bytes);

#endif