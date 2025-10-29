#include "task_i2c_ds3231.h"

static const char *I2C_SENSOR_TASK_TAG = "i2c ds3231 task";

void start_task_i2c_sensors(void)
{
    xTaskCreate(task_i2c_sensors_code, "i2c_sensors", 10000, NULL, 6, &task_i2c_ds3231);
}

void task_i2c_sensors_code(void *pvParameters)
{   
    esp_err_t err = ESP_OK;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay after readings
    }
}