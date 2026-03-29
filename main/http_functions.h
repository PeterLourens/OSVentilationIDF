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
#include "network_manager.h"

#define LOG_BUF_SIZE 256

// Function declarations
void ws_async_send(char *, size_t);
void register_http_handlers(httpd_handle_t server);

esp_err_t http_server_start(void);
esp_err_t http_server_stop(void);

#endif