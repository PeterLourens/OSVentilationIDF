/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file sys_stats.h
 * @brief Public interface for system statistics and task status helpers.
 */

#ifndef SYS_STATS_H
#define SYS_STATS_H

#include <driver/temperature_sensor.h>
#include <esp_heap_caps.h>

#include "globals.h"
#include "general.h"
#include "logging.h"
#include "http_functions.h"
#include "wifi_functions.h"

// Function definitions
const char *state_char_from_enum(eTaskState);
const char *reset_reason_str(esp_reset_reason_t);
void show_system_and_task_stats(void);
void sys_stats_update_actuals(void);
size_t append_heap_stats(char *out, size_t used, size_t buf_size);
size_t append_system_stats(char *out, size_t used, size_t buf_size);
size_t append_task_table(char *out, size_t used, size_t buf_size);
size_t append_header(char *out, size_t used, size_t buf_size);
int compare_tasks(const void *a, const void *b);

#endif