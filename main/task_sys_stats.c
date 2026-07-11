/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_sys_stats.c
 * @brief FreeRTOS task implementation for periodic system statistics refresh.
 */

#include <esp_attr.h>
#include "task_sys_stats.h"

static const char *TAG = "Sys_stats";

EXT_RAM_BSS_ATTR static StackType_t sys_stats_stack[5120]; // stack buffer
static StaticTask_t sys_stats_tcb;                         // TCB buffer
static size_t sys_stats_stack_size = 5120;                 // 5120 words = 20 KB

// Periodic task that refreshes sm_actuals regularly. It calls sys_stats_update_actuals() which already takes statemachine_mutex internally. */
static void task_sys_stats_code(void *pvParameters)
{
    (void)pvParameters;
    LOGI(TAG, "sys_stats_task: starting, poll interval=%d ms", SYS_STATS_POLL_MS);

    const TickType_t delay_ticks = pdMS_TO_TICKS(SYS_STATS_POLL_MS);

    for (;;)
    {
        // Update live stats (heap, stack watermarks, wifi info, cpu temp, etc.)
        sys_stats_update_actuals();

        // Optionally publish or trigger any dependent work here (e.g., update web UI)
        // ws_async_notify_stats_changed();  // if you have such a helper

        vTaskDelay(delay_ticks);
    }

    // never reached
    vTaskDelete(NULL);
}

// Call once at startup to create the task. Safe to call multiple times (idempotent).
void start_task_sys_stats(void)
{
    if (task_sys_stats != NULL)
    {
        LOGI(TAG, "sys_stats_start: already running (task=%p)", (void *)task_sys_stats);
    }

    task_sys_stats = xTaskCreateStatic(task_sys_stats_code, "sys_stats", sys_stats_stack_size, NULL, 8, sys_stats_stack, &sys_stats_tcb);

    if (task_sys_stats == NULL)
    {
        LOGE(TAG, "sys_stats_start: failed to create task");
        task_sys_stats = NULL;
    }

    LOGI(TAG, "sys_stats_start: started (task=%p)", (void *)task_sys_stats);
}

// Optional stop function
void sys_stats_stop(void)
{
    if (task_sys_stats)
    {
        vTaskDelete(task_sys_stats);
        task_sys_stats = NULL;
        LOGI(TAG, "sys_stats_stop: stopped");
    }
}
