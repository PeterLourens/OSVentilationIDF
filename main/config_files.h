#ifndef CONFIG_FILES_H
#define CONFIG_FILES_H

#include "globals.h"
#include "general.h"

// Function definitions
esp_err_t load_sensor_config(const char *);
esp_err_t load_network_config(void);
esp_err_t load_time_config(void);
esp_err_t load_mqtt_config(void);
esp_err_t load_influxdb_config(void);
esp_err_t load_fan_config(void);
esp_err_t load_i2c_config(void);
esp_err_t load_syslog_config(void);
esp_err_t load_statemachine_general_config(void);

bool match_pattern(const char *, const char *);
bool parse_bool_flexible(const cJSON *, bool *);
bool parse_int_flexible(const cJSON *, int *);
bool parse_state_object(const cJSON *, statemachine_config_t *);

#endif