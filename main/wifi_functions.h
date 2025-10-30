#ifndef WIFI_FUNCTIONS_H
#define WIFI_FUNCTIONS_H

#include <stdio.h>

#include "globals.h"

#define MAXIMUM_RETRY 5

#define ESP_AP_SSID "OSVentilation"
#define ESP_AP_PASS "" // Empty for open AP
#define ESP_AP_CHANNEL 1
#define ESP_AP_MAX_CONN 4

// Function declarations
esp_err_t wifi_manager_init(void);
esp_err_t select_wifi_mode(void);
void wifi_event_handler(void *, esp_event_base_t, int32_t, void *);
esp_err_t wifi_manager_connect(Network_config *);
esp_err_t wifi_manager_start_ap(void);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_get_ip_info(char *, size_t);

#endif