/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef WIFI_FUNCTIONS_H
#define WIFI_FUNCTIONS_H

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <sys/socket.h>

#include "logging.h"
#include "network_functions.h"

#define MAXIMUM_RETRY 5

#define ESP_AP_SSID "OSVentilation"
#define ESP_AP_PASS "" // Empty for open AP
#define ESP_AP_CHANNEL 1
#define ESP_AP_MAX_CONN 4

// Function declarations
void wifi_manager_stop(void);
esp_err_t wifi_manager_init(void);
esp_err_t select_wifi_mode(void);
void wifi_event_handler(void *, esp_event_base_t, int32_t, void *);
esp_err_t wifi_manager_connect(Wifi_config_t *);
esp_err_t wifi_manager_start_ap(void);
bool wifi_manager_is_connected(void);
bool wifi_config_is_dhcp_enabled(void);

#endif