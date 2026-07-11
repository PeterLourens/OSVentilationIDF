/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_valvecontrol.h
 * @brief Public task interface for valve control event handling.
 */

#ifndef TASK_VALVECONTROL_H
#define TASK_VALVECONTROL_H

#include "valvecontrol.h"

// function declarations
void start_task_valvecontrol(void);
void task_valvecontrol_code(void *pvParameters);

#endif