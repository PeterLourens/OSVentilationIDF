/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef TASK_INFLUXDB_H
#define TASK_INFLUXDB_H

#include "globals.h"
#include "influxdb.h"
#include "logging.h"

// Function definitions
void start_task_influxdb(void);
void task_influxdb_code(void *);

#endif