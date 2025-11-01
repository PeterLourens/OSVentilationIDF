#ifndef GENERAL_H
#define GENERAL_H

#include "globals.h"

// Function declarations
esp_err_t init_nvs(void);
esp_err_t init_littlefs(void);
esp_err_t read_file(const char *, char *, size_t);
esp_err_t load_sensor_config(const char *);
esp_err_t load_network_config(void);
esp_err_t load_time_config(void);
bool is_valid_ip_address(const char *);
esp_err_t create_mutexes(void);
void formatted_uptime(char *, size_t);
void on_ping_end(esp_ping_handle_t, void *);
void ping_gateway(void);

#endif