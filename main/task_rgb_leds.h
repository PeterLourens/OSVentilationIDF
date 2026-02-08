/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef TASK_RGB_LEDS_H
#define TASK_RGB_LEDS_H

#include "globals.h"
#include "rgb_leds.h"
#include "logging.h"

// Function declarations
void start_task_rgb_leds(void);
void task_rgb_leds_code(void *);

#endif