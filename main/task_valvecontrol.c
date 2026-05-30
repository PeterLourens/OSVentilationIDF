/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include <esp_attr.h>
#include "task_valvecontrol.h"

static const char *TAG = "task_valvectrl";
EXT_RAM_BSS_ATTR static StackType_t valvectrl_stack[4096]; // stack buffer
static StaticTask_t valvectrl_tcb;                         // TCB buffer
static size_t valvectrl_stack_size = 4096;                 // 4096 words = 16 KB

void start_task_valvecontrol(void)
{
    task_valvectrl = xTaskCreateStatic(task_valvecontrol_code, "task_valvectrl", valvectrl_stack_size, NULL, 12, valvectrl_stack, &valvectrl_tcb);

    if (task_valvectrl == NULL)
    {
        LOGE(TAG, "Valvecontrol task creation failed");
    }
}

void task_valvecontrol_code(void *pvParameters)
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        control_valves();
    }
}
