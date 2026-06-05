/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file http_functions.h
 * @brief Public interface for the embedded HTTP and WebSocket server.
 */

#ifndef HTTP_FUNCTIONS_H
#define HTTP_FUNCTIONS_H

#include <esp_http_server.h>
#include <stdint.h>
#include <esp_heap_caps.h>

#include "globals.h"
#include "config_files.h"
#include "http_helper.h"
#include "http_send_ws_data.h"
#include "http_post_functions.h"
#include "valvecontrol.h"
#include "i2c_sensors.h"
#include "logging.h"
#include "statemachine.h"
#include "network_manager.h"
#include "stats_worker.h"
#include "sys_stats.h"
#include "task_uart_sender.h"

#define LOG_BUF_SIZE 256

/**
 * @brief Send raw text to all connected WebSocket clients.
 *
 * @param buf Pointer to the data buffer.
 * @param len Number of bytes in the buffer.
 */
void ws_async_send(const char *buf, size_t len);

/**
 * @brief Start the embedded HTTP server and bind handlers.
 *
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t http_server_start(void);

/**
 * @brief Register HTTP URI handlers on an active server.
 *
 * @param server The server handle returned by esp_http_server_start.
 */
void register_http_handlers(httpd_handle_t server);

/**
 * @brief Stop the embedded HTTP server and release resources.
 *
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t http_server_stop(void);

#endif