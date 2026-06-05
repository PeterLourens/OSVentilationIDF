/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file config_files.h
 * @brief Public configuration loading and parsing interfaces.
 *
 * This header exposes the functions used to load JSON-backed configuration
 * files for sensors, networking, MQTT, InfluxDB, fan control, I2C, syslog, and
 * the statemachine. It also provides flexible JSON helper functions for
 * parsing booleans, integers, and state objects.
 */

#ifndef CONFIG_FILES_H
#define CONFIG_FILES_H

#include <cJSON.h>
#include <dirent.h>
#include <esp_log.h>
#include <esp_heap_caps.h>

#include "globals.h"
#include "general.h"
#include "logging.h"

/**
 * @brief Load sensor configuration for the specified bus.
 *
 * @param config_name The base name of the sensor config file without extension
 *                    (for example "bus0_sensor_config" or "bus1_sensor_config").
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_sensor_config(const char *);

/**
 * @brief Load Wi-Fi networking configuration from /data/json/wifi_config.json.
 *
 * This updates the in-memory wifi_config structure under the network config
 * mutex.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_wifi_config(void);

/**
 * @brief Load Ethernet networking configuration from /data/json/ethernet_config.json.
 *
 * This reads static IP/DHCP settings and stores them under the network config mutex.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_ethernet_config(void);

/**
 * @brief Load time synchronization configuration from /data/json/time_config.json.
 *
 * This populates the runtime time_config structure with NTP server and timezone.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_time_config(void);

/**
 * @brief Load MQTT configuration from /data/json/mqtt_config.json.
 *
 * This updates the in-memory MQTT settings used for messaging and telemetry.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_mqtt_config(void);

/**
 * @brief Load InfluxDB reporting configuration from /data/json/influxdb_config.json.
 *
 * This updates the stored InfluxDB connection parameters and credentials.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_influxdb_config(void);

/**
 * @brief Load fan control configuration from /data/json/fan_config.json.
 *
 * This updates the runtime fan_control settings used by the fan control module.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_fan_config(void);

/**
 * @brief Load I2C bus configuration from /data/json/i2c_config.json.
 *
 * This updates the I2C multiplexing and LCD enable settings.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_i2c_config(void);

/**
 * @brief Load syslog forwarding configuration from /data/json/syslog_config.json.
 *
 * This updates syslog endpoint settings and device identity strings.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_syslog_config(void);

/**
 * @brief Load general statemachine settings from /data/json/sm_general_config.json.
 *
 * This populates the runtime statemachine general configuration values.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_statemachine_general_config(void);

/**
 * @brief Match a string against a simple wildcard pattern.
 *
 * Supports '*' for any sequence of characters and '?' for a single character.
 * @param name The input string to test.
 * @param pattern The wildcard pattern to match against.
 * @return true if @p name matches @p pattern, false otherwise.
 */
bool match_pattern(const char *, const char *);

/**
 * @brief Parse a flexible boolean value from JSON.
 *
 * This accepts JSON booleans, numeric values, and common string values like
 * "On", "Off", "true", "false", "1", and "0".
 * @param item The cJSON item to parse.
 * @param out Destination for the parsed boolean.
 * @return true if parsing succeeded, false otherwise.
 */
bool parse_bool_flexible(const cJSON *, bool *);

/**
 * @brief Parse a flexible integer value from JSON.
 *
 * This accepts JSON numbers or string-encoded integers.
 * @param item The cJSON item to parse.
 * @param out Destination for the parsed integer.
 * @return true if parsing succeeded, false otherwise.
 */
bool parse_int_flexible(const cJSON *, int *);

/**
 * @brief Parse a single JSON object into a statemachine configuration structure.
 *
 * This translates a JSON state object containing fields such as state_name,
 * fanspeed, start/end times, valve positions, and enable_state into the runtime
 * Statemachine_config_t representation.
 * @param obj The JSON object to parse.
 * @param out Destination statemachine config.
 * @return true if the object was parsed successfully, false otherwise.
 */
bool parse_state_object(const cJSON *, Statemachine_config_t *);

/**
 * @brief Copy a snapshot of a statemachine configuration entry.
 *
 * @param index The state index to copy.
 * @param out Destination buffer for the state config.
 */
void get_state_config(size_t, Statemachine_config_t *);

/**
 * @brief Read state configuration files from a directory and populate statemachine_config.
 *
 * This supports JSON files whose root can be an object or an array of objects and
 * loads them in order until the state array is full or the file limit is reached.
 * @param mount_point Directory to scan, e.g. "/data/json".
 * @param pattern Wildcard pattern for matching file names.
 * @param max_files Maximum number of matched files to process. Use <=0 to process all.
 * @param out_states Number of states parsed into the runtime config array.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t read_state_configs_from_json(const char *, const char *, int, size_t *);

/**
 * @brief Load state configuration files from the configured filesystem.
 *
 * This is the high-level entry point for loading the statemachine state definitions.
 * @return ESP_OK on success, otherwise an esp_err_t.
 */
esp_err_t load_state_config(void);

#endif