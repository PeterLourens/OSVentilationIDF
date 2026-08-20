/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_influxdb.h
 * @brief Public task interface for the InfluxDB sender task.
 */

#ifndef TASK_INFLUXDB_H
#define TASK_INFLUXDB_H

#include "influxdb.h"
#include "logging.h"

// Function definitions
void start_task_influxdb(void);
void stop_task_influxdb(void);
bool refresh_influxdb_task_state(void);
void task_influxdb_code(void *);

#endif