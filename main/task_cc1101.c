/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_cc1101.c
 * @brief FreeRTOS task implementation for CC1101 hardware bring-up.
 */

#include <esp_attr.h>

#include "cc1101.h"
#include "logging.h"
#include "task_cc1101.h"

static const char *TAG = "task_cc1101";
static StaticTask_t cc1101_tcb;
EXT_RAM_BSS_ATTR static StackType_t cc1101_stack[2048];
static const size_t cc1101_stack_size = 2048;

void start_task_cc1101(void)
{
    TaskHandle_t task = xTaskCreateStatic(
        task_cc1101_code,
        "cc1101",
        cc1101_stack_size,
        NULL,
        5,
        cc1101_stack,
        &cc1101_tcb);

    if (task == NULL)
    {
        LOGE(TAG, "CC1101 task creation failed");
    }
}

void task_cc1101_code(void *pvParameters)
{
    (void)pvParameters;

    esp_err_t err = cc1101_init();
    if (err != ESP_OK)
    {
        LOGW(TAG, "CC1101 hardware test failed: %s", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
}
