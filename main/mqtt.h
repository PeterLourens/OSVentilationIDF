#ifndef MQTT_H
#define MQTT_H

#include <mqtt_client.h>
#include <mqtt5_client.h>

#include "globals.h"
#include "general.h"

// Function declarations
bool is_enabled_str(const char *s);
void normalize_base_topic(const char *in, char *out, size_t out_len);
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
esp_err_t mqtt_start(const MQTT_config_t *cfg);
void mqtt_stop(void);
bool mqtt_is_connected(void);
char *format_sensor_3d_json(const float a[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], size_t *out_len);
void mqtt_publish_sensor_values(const MQTT_config_t *cfg);

#endif