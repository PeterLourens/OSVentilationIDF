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
 * This helper broadcasts a UTF-8 payload to every active WebSocket client
 * registered on the embedded HTTP server.
 *
 * @param buf Pointer to the data buffer to send.
 * @param len Number of bytes in the buffer.
 */
void ws_async_send(const char *buf, size_t len);

/**
 * @brief Start the embedded HTTP server and bind handlers.
 *
 * This function starts the HTTP server on port 80 and registers the web
 * UI, WebSocket, and configuration endpoints.
 *
 * @return ESP_OK if the server started successfully.
 * @return ESP_ERR_INVALID_STATE if the active network interface has no IP.
 * @return other esp_err_t values on HTTP server startup failure.
 */
esp_err_t http_server_start(void);

/**
 * @brief Register HTTP URI handlers on an active server.
 *
 * Call this after the HTTP server has been started to bind all embedded
 * resource handlers and POST endpoints used by the web UI.
 *
 * @param server The server handle returned by esp_http_server_start.
 */
void register_http_handlers(httpd_handle_t server);

/**
 * @brief Stop the embedded HTTP server and release resources.
 *
 * This function stops the HTTP server instance and clears the global
 * server handle so the server can be restarted later if needed.
 *
 * @return ESP_OK if the server was stopped successfully.
 * @return ESP_ERR_INVALID_ARG if the server handle was not valid.
 * @return other esp_err_t values on shutdown failure.
 */
esp_err_t http_server_stop(void);

#endif