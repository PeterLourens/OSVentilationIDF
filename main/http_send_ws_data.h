/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef HTTP_SEND_WS_DATA_H
#define HTTP_SEND_WS_DATA_H

#define DETAILS_CHUNK_SIZE 1024

#include <stdint.h>

#include "globals.h"
#include "general.h"
#include "http_functions.h"
#include "logging.h"
#include "esp_timer.h"

// Function definitions
void send_sensor_config_json_ws(void);
void send_config_json_ws(void);
void send_index_html_ws(void);
// void send_statemachine_config_json_ws(void);
void send_statemachine_config_json_ws(httpd_req_t *req);
void send_stats_json_ws(void);

#endif
