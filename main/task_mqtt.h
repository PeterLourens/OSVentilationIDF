/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_mqtt.h
 * @brief Public MQTT task declarations and connection helpers.
 */

#ifndef TASK_MQTT_H
#define TASK_MQTT_H

#include "rgb_leds.h"
#include "mqtt.h"
#include "logging.h"
#include "network_manager.h"
#include "network_functions.h"

// Function declarations
void start_task_mqtt(void);
void task_mqtt_code(void *);
bool mqtt_wait_for_connection(TickType_t);

#endif