/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_sys_stats.h
 * @brief Public declarations for the system statistics task.
 */

#ifndef TASK_SYS_STATS_H
#define TASK_SYS_STATS_H

#define SYS_STATS_POLL_MS (10000) // 10 seconds

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "globals.h"
#include "sys_stats.h"
#include "logging.h"

// Function declarations
void start_task_sys_stats(void);
void sys_stats_stop(void);

#endif