#ifndef TASK_WIFI_H
#define TASK_WIFI_H

#include <stdio.h>

#include "globals.h"
#include "wifi_functions.h"

// Function declarations
void start_task_wifi(void);
void task_wifi_code(void *pvParameters);

#endif