/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef STATS_WORKER_H
#define STATS_WORKER_H

#include "stats_worker.h"
#include "sys_stats.h"         // sys_stats_build_json()
#include "http_send_ws_data.h" // if you have existing send_* helpers
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>

#include "logging.h"

// Function declarations
void stats_worker_init(void);
bool stats_worker_request(const char *cmd); // "stats", "statemachine", "index", etc.

#endif