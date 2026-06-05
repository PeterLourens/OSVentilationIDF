/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file http_post_functions.h
 * @brief Builders for form-encoded POST bodies and I2C/LCD configuration helpers.
 */

#ifndef HTTP_POST_FUNCTIONS_H
#define HTTP_POST_FUNCTIONS_H

#include "globals.h"
#include "general.h"

#include "cJSON.h"
#include "driver/i2c_master.h"
#include "task_i2c_lcd.h"

#define LOG_BUF_SIZE 256

/**
 * @brief Build a bus sensor JSON object from URL-encoded POST data.
 *
 * @param body URL-encoded request body.
 * @param len Length of the body in bytes.
 * @param bus_prefix Prefix for sensor field names (e.g. "b0_s").
 * @return cJSON* New JSON object on success, NULL on failure.
 */
cJSON *build_bus_config_from_body_urlencoded(const char *, size_t, const char *);

/**
 * @brief Build Wi-Fi configuration JSON from form POST data.
 */
cJSON *build_wifi_config_from_body_urlencoded(const char *, size_t);

/**
 * @brief Build Ethernet configuration JSON from form POST data.
 */
cJSON *build_ethernet_config_from_body_urlencoded(const char *, size_t);

/**
 * @brief Apply LCD configuration changes and start/stop the LCD task.
 */
void lcd_apply_config_change(void);

/**
 * @brief Build I2C configuration JSON from form POST data.
 */
cJSON *build_i2c_config_from_body_urlencoded(const char *, size_t);

/**
 * @brief Build MQTT configuration JSON from form POST data.
 */
cJSON *build_mqtt_config_from_body_urlencoded(const char *, size_t);

/**
 * @brief Build InfluxDB configuration JSON from form POST data.
 */
cJSON *build_influxdb_config_from_body_urlencoded(const char *, size_t);

/**
 * @brief Build fan control configuration JSON from form POST data.
 */
cJSON *build_fan_config_from_body_urlencoded(const char *, size_t);

/**
 * @brief Build syslog configuration JSON from form POST data.
 */
cJSON *build_syslog_config_from_body_urlencoded(const char *, size_t);

/**
 * @brief Decode URL-encoded data in place.
 *
 * @param str String containing URL-encoded content.
 */
void url_decode_inplace(char *);

/**
 * @brief Build NTP/timezone configuration JSON from form POST data.
 */
cJSON *build_time_config_from_body_urlencoded(const char *, size_t);

/**
 * @brief Build generated state timing configuration JSON from form POST data.
 */
cJSON *build_state_gen_config_from_body_urlencoded(const char *, size_t);

/**
 * @brief Build a single state configuration JSON object from form POST data.
 *
 * @param state_number Numeric state identifier.
 */
cJSON *build_state_config_from_body_urlencoded(const char *, size_t, int);

#endif