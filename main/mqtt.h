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

/**
 * @file mqtt.h
 * @brief MQTT client and Home Assistant discovery interface declarations.
 */

extern esp_mqtt_client_handle_t s_mqtt;

ESP_EVENT_DECLARE_BASE(MQTT_EVENTS);

/**
 * @brief Request MQTT Home Assistant discovery to be published.
 */
void mqtt_request_homeassistant_discovery(void);

/**
 * @brief Query whether an MQTT discovery request is pending.
 *
 * @return true if discovery has been requested.
 */
bool mqtt_discovery_requested(void);

/**
 * @brief Clear any pending MQTT discovery request.
 */
void mqtt_clear_discovery_request(void);

/**
 * @brief Wait for the MQTT client to establish a connection.
 *
 * @param timeout_ticks Maximum RTOS ticks to wait.
 * @return true if connected before timeout.
 */
bool mqtt_wait_for_connection(TickType_t);

/**
 * @brief Set the MQTT connection state.
 *
 * @param state true if connected.
 */
void mqtt_set_connected(bool state);

/**
 * @brief Query whether MQTT is currently connected.
 *
 * @return true if connected.
 */
bool mqtt_is_connected(void);

/**
 * @brief Check whether MQTT is enabled in the current runtime configuration.
 */
bool mqtt_is_enabled_in_config(void);

/**
 * @brief Start the MQTT client using the provided configuration.
 *
 * @param cfg Pointer to MQTT configuration data.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t mqtt_start(const MQTT_config_t *cfg);

/**
 * @brief Publish current sensor values over MQTT.
 */
void mqtt_publish_sensor_values(void);

/**
 * @brief Publish current valve positions over MQTT.
 */
void mqtt_publish_valve_positions(void);

/**
 * @brief Publish current state and fan speed over MQTT.
 */
void mqtt_publish_state_and_fanspeed(void);

/**
 * @brief Normalize the MQTT base topic into a safe string.
 *
 * @param in Input base topic.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 */
void normalize_base_topic(const char *in, char *out, size_t out_size);

/**
 * @brief Publish MQTT Home Assistant discovery payloads.
 */
void mqtt_publish_homeassistant_discovery(void);

/**
 * @brief Check whether sensor data is ready for MQTT publication.
 *
 * @return true if sensor snapshots are available.
 */
bool sensors_ready(void);

/**
 * @brief Retrieve the cached MQTT node identifier.
 *
 * @return Null-terminated node ID string.
 */
const char *mqtt_get_node_id(void);

/**
 * @brief Forward an MQTT event to the application event loop.
 *
 * @param arg User-provided argument.
 * @param event_base Event base identifier.
 * @param event_id Event identifier.
 * @param event_data Event-specific data pointer.
 */
void mqtt_event_forwarder(void *, esp_event_base_t, int32_t, void *);

/**
 * @brief Copy the current MQTT configuration into the output snapshot.
 *
 * @param[out] out Destination configuration structure.
 */
void mqtt_get_config_snapshot(MQTT_config_t *out);

/**
 * @brief Handle an incoming MQTT message event.
 *
 * @param event MQTT event containing topic and payload.
 */
void mqtt_handle_incoming(esp_mqtt_event_handle_t);

/**
 * @brief Set the state machine speed based on MQTT payload.
 *
 * @param payload Payload containing the requested speed.
 * @param len Payload length in bytes.
 */
void statemachine_set_speed(const char *payload, size_t len);

/**
 * @brief Stop the MQTT client and cleanup resources.
 *
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t mqtt_stop(void);

#endif