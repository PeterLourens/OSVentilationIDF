#ifndef GENERAL_H
#define GENERAL_H

#include "globals.h"
#include "i2c_ds3231.h"
#include "http_functions.h"

// Function declarations
esp_err_t init_nvs(void);
esp_err_t init_littlefs(void);
esp_err_t read_file(const char *, char *, size_t);
esp_err_t create_mutexes(void);

bool is_valid_ip_address(const char *);
bool wifi_has_ip(void);
bool ping_gateway(void);
void formatted_uptime(char *, size_t);
void init_timezone(void);
void on_ping_end(esp_ping_handle_t, void *);
void formatted_daydatetime(char *, size_t);
void formatted_datetime(char *, size_t);
void show_system_and_task_stats(void);
void set_log_levels(void);
void print_html_file(const char *);

#endif