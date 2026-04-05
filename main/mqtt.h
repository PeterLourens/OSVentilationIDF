/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef MQTT_H
#define MQTT_H

#include <mqtt_client.h>
#include <mqtt5_client.h>
#include <esp_event.h>

#include "general.h"
#include "statemachine_states.h"
#include "statemachine_helper.h"
#include "i2c_sensors.h"
#include "logging.h"

extern esp_mqtt_client_handle_t s_mqtt;

ESP_EVENT_DECLARE_BASE(MQTT_EVENTS);

// Function declarations
void mqtt_request_homeassistant_discovery(void);
bool mqtt_discovery_requested(void);
void mqtt_clear_discovery_request(void);
bool mqtt_wait_for_connection(TickType_t);
void mqtt_set_connected(bool state);
bool mqtt_is_connected(void);
esp_err_t mqtt_start(const MQTT_config_t *cfg);
void mqtt_publish_sensor_values(void);
void mqtt_publish_valve_positions(void);
void mqtt_publish_state_and_fanspeed(void);
void normalize_base_topic(const char *in, char *out, size_t out_size);
void mqtt_publish_homeassistant_discovery(void);
bool sensors_ready(void);
const char *mqtt_get_node_id(void);
void mqtt_event_forwarder(void *, esp_event_base_t, int32_t, void *);
void mqtt_get_config_snapshot(MQTT_config_t *out);
void mqtt_handle_incoming(esp_mqtt_event_handle_t);
void statemachine_set_speed(const char *payload, size_t len);
esp_err_t mqtt_stop(void);

#endif