/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef MQTT_H
#define MQTT_H

#include <mqtt_client.h>
#include <mqtt5_client.h>
#include <inttypes.h>

#include "globals.h"
#include "general.h"
#include "statemachine_states.h"
#include "statemachine_helper.h"
#include "logging.h"

extern esp_mqtt_client_handle_t s_mqtt;

// Function declarations
esp_err_t mqtt_start_client(void);
void mqtt_apply_config_change(void);
void mqtt_publish_sensor_values(void);
void mqtt_publish_valve_positions(void);
void mqtt_publish_state_and_fanspeed(void);
void normalize_base_topic(const char *, char *, size_t);
bool mqtt_wait_for_connection(TickType_t);
const char *mqtt_get_node_id(void);
void mqtt_publish_homeassistant_discovery(void);
bool sensors_ready(void);

#endif