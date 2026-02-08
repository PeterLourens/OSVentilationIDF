/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef TASK_WIFI_H
#define TASK_WIFI_H

#include "globals.h"
#include "wifi_functions.h"
#include "logging.h"

// Function declarations
void start_task_wifi(void);
void task_wifi_code(void *pvParameters);

#endif