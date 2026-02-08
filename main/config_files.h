/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef CONFIG_FILES_H
#define CONFIG_FILES_H

#include <cJSON.h>
#include <dirent.h>
#include <esp_log.h>

#include "globals.h"
#include "general.h"
#include "logging.h"

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
void get_state_config(size_t, Statemachine_config_t *);
bool parse_state_object(const cJSON *, Statemachine_config_t *);
esp_err_t read_state_configs_from_json(const char *, const char *, int, size_t *);
esp_err_t load_state_config(void);

#endif