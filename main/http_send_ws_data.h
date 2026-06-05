/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file http_send_ws_data.h
 * @brief Header for WebSocket JSON response helper functions.
 */

#ifndef HTTP_SEND_WS_DATA_H
#define HTTP_SEND_WS_DATA_H

#define DETAILS_CHUNK_SIZE 1024
#define MAX_WS_CLIENTS_TRACK 16
// #define ENABLE_PER_CONN_RATE_LIMIT 0 // enable optional per-connection rate limiting (define before including this header)

#include <stdint.h>
#include <esp_heap_caps.h>

#include "globals.h"
#include "general.h"
#include "http_functions.h"
#include "logging.h"
#include "esp_timer.h"

/**
 * @brief Get the latest CPU temperature sampled by the sys_stats task.
 *
 * @return float Current CPU temperature in degrees Celsius.
 */
float get_cpu_temp_c(void);

/**
 * @brief Send the main index page data over WebSocket to a requesting client.
 */
void send_index_html_ws(httpd_req_t *req);

/**
 * @brief Send current sensor configuration as JSON over WebSocket.
 */
void send_sensor_config_json_ws(httpd_req_t *req);

/**
 * @brief Send current runtime configuration as JSON over WebSocket.
 */
void send_config_json_ws(httpd_req_t *req);

/**
 * @brief Send current statemachine state and settings as JSON over WebSocket.
 */
void send_statemachine_config_json_ws(httpd_req_t *req);

/**
 * @brief Send collected system statistics as JSON over WebSocket.
 */
void send_stats_json_ws(httpd_req_t *req);

#endif
