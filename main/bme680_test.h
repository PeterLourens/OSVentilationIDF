#ifndef BME680_TEST_H
#define BME680_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"

#include "bme68x.h"        // Bosch types
#include "bme680_driver.h" // Declare bme68x_get_data_retry or wrapper
#include "globals.h"       // i2c handle types
#include "i2c_sensors.h"

void start_bme680_profiles_test(i2c_master_bus_handle_t, i2c_master_dev_handle_t, i2c_master_dev_handle_t, int, SemaphoreHandle_t);

#endif