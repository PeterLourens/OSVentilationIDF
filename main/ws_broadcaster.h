/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef WS_BROADCASTER_H
#define WS_BROADCASTER_H

#include "cJSON.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "logging.h"
#include "http_functions.h" // ws_async_send(buf,len)

// Function declarations
void ws_broadcaster_init(void);
void ws_broadcaster_send(cJSON *obj); // takes ownership of obj
void ws_broadcaster_send_raw(const char *buf, size_t len);

#endif