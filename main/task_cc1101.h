/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_cc1101.h
 * @brief FreeRTOS task interface for CC1101 hardware bring-up.
 */

#ifndef TASK_CC1101_H
#define TASK_CC1101_H

void start_task_cc1101(void);
void task_cc1101_code(void *pvParameters);

#endif
