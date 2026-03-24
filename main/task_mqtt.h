/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef TASK_MQTT_H
#define TASK_MQTT_H

#include "globals.h"
#include "rgb_leds.h"
#include "mqtt.h"
#include "logging.h"
#include "network_manager.h"

// Function declarations
void start_task_mqtt(void);
void task_mqtt_code(void *);
bool mqtt_wait_for_connection(TickType_t);

#endif