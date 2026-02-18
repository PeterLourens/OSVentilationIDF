/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef HTTP_FUNCTIONS_H
#define HTTP_FUNCTIONS_H

#include <esp_http_server.h>
#include <stdint.h>

#include "globals.h"
#include "config_files.h"
#include "http_helper.h"
#include "http_send_ws_data.h"
#include "http_post_functions.h"
#include "valvecontrol.h"
#include "i2c_sensors.h"
#include "logging.h"
#include "statemachine.h"

#define LOG_BUF_SIZE 256

// Function declarations

// GET handlers
esp_err_t pure_min_css_get_handler(httpd_req_t *);
esp_err_t styles_css_get_handler(httpd_req_t *);
esp_err_t index_get_handler(httpd_req_t *);
esp_err_t webserial_get_handler(httpd_req_t *);
esp_err_t sensor_config_get_handler(httpd_req_t *);
esp_err_t settings_get_handler(httpd_req_t *);
esp_err_t websocket_handler(httpd_req_t *);

// POST handlers
esp_err_t sensorconfig_bus0_post_handler(httpd_req_t *);
esp_err_t sensorconfig_bus1_post_handler(httpd_req_t *);
esp_err_t delete_bus0_config_post_handler(httpd_req_t *);
esp_err_t delete_bus1_config_post_handler(httpd_req_t *);
esp_err_t reset_valves_handler(httpd_req_t *);
esp_err_t stop_statemachine_handler(httpd_req_t *);
esp_err_t start_statemachine_handler(httpd_req_t *);
esp_err_t valvecontrol_post_handler(httpd_req_t *);
esp_err_t wifi_config_post_handler(httpd_req_t *);
esp_err_t ethernet_config_post_handler(httpd_req_t *);
esp_err_t i2c_config_post_handler(httpd_req_t *);
esp_err_t mqtt_config_post_handler(httpd_req_t *);
esp_err_t influxdb_config_post_handler(httpd_req_t *);
esp_err_t syslog_config_post_handler(httpd_req_t *);
esp_err_t time_config_post_handler(httpd_req_t *);
esp_err_t fan_config_post_handler(httpd_req_t *);
esp_err_t state_gen_config_post_handler(httpd_req_t *);
esp_err_t state_config_post_handler(httpd_req_t *);

void ws_async_send(char *, size_t);
httpd_handle_t start_webserver(void);
esp_err_t http_server_stop(void);

#endif