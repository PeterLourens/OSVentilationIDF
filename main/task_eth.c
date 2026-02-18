/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_eth.h"
#include "globals.h"

static const char *TAG = "task_eth";

/* Task storage for static creation */
static StackType_t eth_stack[4096];
static StaticTask_t eth_tcb;
static size_t eth_stack_size = 4096;

static volatile bool eth_connected = false;
static volatile bool wifi_forced_on = false;

void start_task_eth(void)
{
    task_ethernet = xTaskCreateStatic(task_eth_code, "task_eth", eth_stack_size, NULL, 9, eth_stack, &eth_tcb);

    if(task_ethernet == NULL)
    {
        LOGE(TAG, "Failed to create Ethernet task");
    }
}

void task_eth_code(void *pvParameters)
{
    (void)pvParameters;
    LOGI(TAG, "Ethernet task starting");

    // All heavy lifting is in eth_manager_start()
    eth_manager_start();

    // Keep the task alive for diagnostics; event handlers do the work
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}