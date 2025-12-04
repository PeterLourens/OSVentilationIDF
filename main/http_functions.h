#ifndef HTTP_FUNCTIONS_H
#define HTTP_FUNCTIONS_H

#include "globals.h"
#include "general.h"
#include "sys_stats.h"
#include "config_files.h"
#include "http_send_ws_data.h"

#define LOG_BUF_SIZE 256

// Function declarations
esp_err_t pure_min_css_get_handler(httpd_req_t *);
esp_err_t styles_css_get_handler(httpd_req_t *);
esp_err_t index_get_handler(httpd_req_t *);
esp_err_t webserial_get_handler(httpd_req_t *);
esp_err_t websocket_handler(httpd_req_t *);
esp_err_t sensorconfig_bus0_post_handler(httpd_req_t *);
esp_err_t sensorconfig_bus1_post_handler(httpd_req_t *);
esp_err_t delete_bus0_config_post_handler(httpd_req_t *);
esp_err_t delete_bus1_config_post_handler(httpd_req_t *);
esp_err_t reset_valves_handler(httpd_req_t *);
void ws_async_send(char *, size_t);
httpd_handle_t start_webserver(void);
esp_err_t http_server_stop(void);
esp_err_t sensor_config_get_handler(httpd_req_t *);
esp_err_t settings_get_handler(httpd_req_t *);
size_t remove_ansi_escape_codes(const char *, char *, size_t);
int websocket_log_output(const char *, va_list);
int uart_log_output(const char *, va_list);
bool write_json_atomic_fs(const char *, cJSON *);
cJSON *build_bus_config_from_body_urlencoded(const char *, size_t, const char *);

// Functions to send JSON via WebSocket

#endif