/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef INFLUXDB_H
#define INFLUXDB_H

#define DMA_MONITOR_PAYLOAD_SZ 256

#include <time.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>

#include "i2c_ds3231.h"
#include "logging.h"

extern QueueHandle_t influx_queue;
extern TaskHandle_t influx_sender_task_handle;

// Expose monitor API and runtime-configurable intervals
extern uint32_t g_influx_task_interval_ms;    // base Influx task interval (ms)
extern uint32_t g_dma_monitor_interval_ms;    // DMA monitor interval (ms)
extern uint32_t g_monitor_tick_resolution_ms; // resolution used by integrated loop (ms)

// Function definitions
bool influxdb_enqueue_sensor_line(int bus, int ch, float sensor_val[DATA_FIELDS], float avg_val[DATA_FIELDS]);
bool influxdb_wait_ready(TickType_t);
void influxdb_set_dma_monitor_interval_ms(uint32_t ms);
uint32_t influxdb_get_dma_monitor_interval_ms(void);
void influxdb_set_influx_task_interval_ms(uint32_t ms);
uint32_t influxdb_get_influx_task_interval_ms(void);
bool influxdb_enqueue_dma_metrics(const char *host_tag);
int64_t influxdb_get_timestamp(void);
bool influxdb_init_sender(void);
bool influxdb_enqueue_payload(const char *payload, size_t len);
void influxdb_process(QueueHandle_t sensor_q, QueueHandle_t avg_q);
void influxdb_get_config_snapshot(Influxdb_config_t *out);
void influxdb_build_url(char *out, size_t out_size, const Influxdb_config_t *cfg);
void influxdb_build_payload_actuals(char *out, size_t out_size);
void influxdb_build_payload_valves(char *, size_t);
void influxdb_build_payload_sensors_for_bus(char *out, size_t out_size, float sensor[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], int bus);

#endif
