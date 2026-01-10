#ifndef MQTT_H
#define MQTT_H

#include <mqtt_client.h>
#include <mqtt5_client.h>

#include "globals.h"
#include "general.h"

extern esp_mqtt_client_handle_t s_mqtt;

// Function declarations
esp_err_t mqtt_start_client(void);
void mqtt_apply_config_change(void);
void mqtt_publish_sensor_values(void);
void mqtt_publish_valve_positions(void);
void normalize_base_topic(const char *in, char *out, size_t out_size);

#endif