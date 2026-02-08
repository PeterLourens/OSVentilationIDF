/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef TASK_VALVECONTROL_H
#define TASK_VALVECONTROL_H

#include "globals.h"
#include "general.h"
#include "valvecontrol.h"

// function declarations
void start_task_valvecontrol(void);
void task_valvecontrol_code(void *pvParameters);

#endif