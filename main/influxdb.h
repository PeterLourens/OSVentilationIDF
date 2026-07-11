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

/**
 * @file influxdb.h
 * @brief InfluxDB telemetry enqueue/send API for OSVentilation.
 */

extern QueueHandle_t influx_queue;
extern TaskHandle_t influx_sender_task_handle;

// Expose monitor API and runtime-configurable intervals
extern uint32_t g_influx_task_interval_ms;    // base Influx task interval (ms)
extern uint32_t g_dma_monitor_interval_ms;    // DMA monitor interval (ms)
extern uint32_t g_monitor_tick_resolution_ms; // resolution used by integrated loop (ms)

/**
 * @brief Build and enqueue a single sensor measurement line for InfluxDB.
 *
 * @param bus Sensor bus index.
 * @param ch Sensor channel index.
 * @param sensor_val Raw sensor values.
 * @param avg_val Averaged sensor values.
 * @return true if the line was enqueued successfully.
 */
bool influxdb_enqueue_sensor_line(int bus, int ch, float sensor_val[DATA_FIELDS], float avg_val[DATA_FIELDS]);

/**
 * @brief Wait until the Influx queue has been created and is ready.
 *
 * @param timeout_ticks Maximum ticks to wait.
 * @return true if ready before timeout, false otherwise.
 */
bool influxdb_wait_ready(TickType_t);

/**
 * @brief Set the DMA monitor interval used by the InfluxDB monitor payload.
 *
 * @param ms Interval in milliseconds.
 */
void influxdb_set_dma_monitor_interval_ms(uint32_t ms);

/**
 * @brief Get the currently configured DMA monitor interval.
 *
 * @return Interval in milliseconds.
 */
uint32_t influxdb_get_dma_monitor_interval_ms(void);

/**
 * @brief Set the base Influx task interval.
 *
 * @param ms Interval in milliseconds.
 */
void influxdb_set_influx_task_interval_ms(uint32_t ms);

/**
 * @brief Get the configured base Influx task interval.
 *
 * @return Interval in milliseconds.
 */
uint32_t influxdb_get_influx_task_interval_ms(void);

/**
 * @brief Enqueue a DMA metrics payload for InfluxDB.
 *
 * @param host_tag Optional host tag string.
 * @return true if queued successfully.
 */
bool influxdb_enqueue_dma_metrics(const char *host_tag);

/**
 * @brief Get a high-resolution timestamp for InfluxDB line protocol.
 *
 * @return Nanoseconds since the Unix epoch.
 */
int64_t influxdb_get_timestamp(void);

/**
 * @brief Initialize the InfluxDB sender task and queue.
 *
 * @return true if initialization succeeded.
 */
bool influxdb_init_sender(void);

/**
 * @brief Enqueue a raw InfluxDB payload line.
 *
 * @param payload Null-terminated payload string.
 * @param len Length of the payload in bytes.
 * @return true if the payload was queued successfully.
 */
bool influxdb_enqueue_payload(const char *payload, size_t len);

/**
 * @brief Process queued telemetry into InfluxDB payloads and enqueue them for sending.
 *
 * @param sensor_q Sensor data queue handle.
 * @param avg_q Sensor average data queue handle.
 */
void influxdb_process(QueueHandle_t sensor_q, QueueHandle_t avg_q);

/**
 * @brief Copy the current InfluxDB configuration into an output snapshot.
 *
 * @param[out] out Output configuration structure.
 */
void influxdb_get_config_snapshot(Influxdb_config_t *out);

/**
 * @brief Build the InfluxDB write URL from the current configuration.
 *
 * @param[out] out URL buffer.
 * @param out_size Size of the output buffer.
 * @param cfg Configuration snapshot.
 */
void influxdb_build_url(char *out, size_t out_size, const Influxdb_config_t *cfg);

/**
 * @brief Build the payload for actuals metrics.
 *
 * @param[out] out Payload buffer.
 * @param out_size Size of the output buffer.
 */
void influxdb_build_payload_actuals(char *out, size_t out_size);

/**
 * @brief Build the payload for valve position metrics.
 *
 * @param[out] out Payload buffer.
 * @param out_size Size of the output buffer.
 */
void influxdb_build_payload_valves(char *, size_t);

/**
 * @brief Build a sensor payload chunk for a single bus.
 *
 * @param[out] out Payload buffer.
 * @param out_size Output buffer size.
 * @param sensor Raw sensor snapshot.
 * @param avg Averaged sensor snapshot.
 * @param bus Bus index.
 */
void influxdb_build_payload_sensors_for_bus(char *out, size_t out_size, float sensor[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], int bus);

#endif
