/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef INFLUXDB_H
#define INFLUXDB_H

#include <time.h>
#include <esp_log.h>
#include <esp_http_client.h>

#include "globals.h"
#include "i2c_ds3231.h"
#include "logging.h"

// Function definitions
void influxdb_get_config_snapshot(Influxdb_config_t *);
int64_t influxdb_get_timestamp(void);
void influxdb_build_url(char *out, size_t out_size, const Influxdb_config_t *cfg);
void influxdb_format_line(char *out, size_t out_size, int bus, int ch, float raw0, float raw1, float raw2, float avg0, float avg1, float avg2, int64_t ts);
void influxdb_append_line(char *, size_t, const char *);
void influxdb_build_payload(char *out, size_t out_size, float sensor[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]);
void influxdb_send_payload(const char *payload, const Influxdb_config_t *cfg);
void influxdb_process(QueueHandle_t sensor_q, QueueHandle_t avg_q);

#endif