#ifndef TASK_VALVECONTROL_H // To make sure you don't declare the function more than once by including the header multiple times.
#define TASK_VALVECONTROL_H

#include "globals.h"
#include "general.h"
#include "valvecontrol.h"

// function declarations
void start_task_valvecontrol(void);
void task_valvecontrol_code(void *pvParameters);

#endif