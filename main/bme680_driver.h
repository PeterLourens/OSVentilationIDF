#ifndef BME680_DRIVER_H
#define BME680_DRIVER_H

#include <esp_err.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_rom_sys.h>
#include <stdatomic.h>

#include "logging.h"
#include "globals.h"

#include "bme68x.h"
#include "bme68x_defs.h"
#include "bsec_adapter.h"
#include "i2c_sensors.h"

// BME680
#define BME680_PROFILE_SEQUENCE 1
#define BME680_PROFILE_TASK_NAME "bme680_prof"
#define BME680_PROFILE_STACK_BYTES 4096                /* minimal: 4 KB; increase to 6144–8192 if using heavy libs */
#define BME680_PROFILE_PRIORITY (tskIDLE_PRIORITY + 1) /* low priority */

extern struct bme68x_dev bme68x_g;

esp_err_t bme680_driver_init(i2c_master_dev_handle_t);
esp_err_t bme680_forced_read(float *, float *, float *, float *);

void bme680_run_profile_sequence(void);
int8_t bme68x_get_data_retry(uint8_t op_mode, struct bme68x_data *data, uint8_t *n_fields, int retries);
void schedule_bme680_profile_once(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t sensor_dev, i2c_master_dev_handle_t mux_dev, int mux_channel);
int get_bme680_profile_success_count(void);
int get_bme680_profile_fail_count(void);

#endif /* BME680_DRIVER_H */
