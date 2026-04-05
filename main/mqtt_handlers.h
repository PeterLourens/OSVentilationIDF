/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef MQTT_HANDLERS_H
#define MQTT_HANDLERS_H

#include <mqtt_client.h>

#include "mqtt.h"
#include "statemachine.h"
#include "esp_log.h"

void mqtt_handle_incoming(esp_mqtt_event_handle_t);

#endif