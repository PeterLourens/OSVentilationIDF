/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef INFLUXDB_H
#define INFLUXDB_H

#include <time.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "i2c_ds3231.h"
#include "logging.h"

extern QueueHandle_t influx_queue;
extern TaskHandle_t influx_sender_task_handle;

// Function definitions
bool influxdb_wait_ready(TickType_t);
bool influxdb_init_sender(void);
bool influxdb_enqueue_payload(const char *payload, size_t len);
void influxdb_process(QueueHandle_t sensor_q, QueueHandle_t avg_q);
void influxdb_get_config_snapshot(Influxdb_config_t *out);
void influxdb_build_url(char *out, size_t out_size, const Influxdb_config_t *cfg);
void influxdb_build_payload_actuals(char *out, size_t out_size);
void influxdb_build_payload_valves(char *, size_t);
void influxdb_build_payload_sensors_for_bus(char *out, size_t out_size, float sensor[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], int bus);

#endif
