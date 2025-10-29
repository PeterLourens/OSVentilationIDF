#ifndef WIFI_FUNCTIONS_H
#define WIFI_FUNCTIONS_H

#include <stdio.h>

#include "globals.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define AP_STARTED_BIT BIT2

// Access Point Configuration
#define ESP_AP_SSID "OSVentilation"
#define ESP_AP_PASS ""
#define ESP_AP_CHANNEL 1
#define ESP_AP_MAX_CONN 4

// Function declarations
esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect(Network_settings *settings);
esp_err_t wifi_manager_disconnect(void);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_get_ip_info(char *ip_str, size_t max_len);

#endif