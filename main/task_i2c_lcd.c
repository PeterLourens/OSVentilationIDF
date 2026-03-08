/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_i2c_lcd.h"

static const char *TAG = "task_i2c_LCD";

static StackType_t i2c_lcd_stack[5120];  // stack buffer
static StaticTask_t i2c_lcd_tcb;         // TCB buffer
static size_t i2c_lcd_stack_size = 5120; // 5120 words = 20 KB

void stop_task_i2c_lcd(void)
{
    if (task_i2c_lcd == NULL)
    {
        LOGI(TAG, "LCD task already stopped");
        return;
    }

    // Optional: turn off LCD hardware
    i2c_lcd_clear();
    i2c_lcd_backlight_set(false);

    LOGI(TAG, "Stopping LCD task...");
    vTaskDelete(task_i2c_lcd);
    task_i2c_lcd = NULL;
}

void start_task_i2c_lcd(void)
{
    I2C_config_t cfg;

    xSemaphoreTake(i2c_config_mutex, portMAX_DELAY);
    memcpy(&cfg, &i2c_config, sizeof(cfg));
    xSemaphoreGive(i2c_config_mutex);

    if (strcmp(cfg.enable_lcd, "On") != 0)
    {
        LOGI(TAG, "LCD disabled → task not started");
        return;
    }

    if (task_i2c_lcd != NULL)
    {
        LOGW(TAG, "LCD task already running");
        return;
    }

    task_i2c_lcd = xTaskCreateStatic(task_i2c_lcd_code, "i2c_lcd", i2c_lcd_stack_size, NULL, 6, i2c_lcd_stack, &i2c_lcd_tcb);

    if (!task_i2c_lcd)
        LOGE(TAG, "LCD task creation failed");
    else
        LOGI(TAG, "LCD task started");
}

void task_i2c_lcd_code(void *pvParameters)
{
    I2C_config_t cfg;

    char uptime[20] = {};
    char ping_result[20] = {};
    char formatted_time[32] = {};
    char wifi_ip[16] = "0.0.0.0";
    char eth_ip[16] = "0.0.0.0";
    char buf[100] = {}; // 4x20 LCD + null terminator is 81 bytes so 100 is plenty
    // char ds3231_status[30] = {};

    xSemaphoreTake(i2c_config_mutex, portMAX_DELAY);
    memcpy(&cfg, &i2c_config, sizeof(cfg));
    xSemaphoreGive(i2c_config_mutex);

    if (strcmp(cfg.enable_lcd, "Off") == 0)
    {
        LOGW(TAG, "LCD disabled → terminating LCD task");
        task_i2c_lcd = NULL;
        vTaskDelete(NULL);
    }

    float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    LOGI(TAG, "LCD task started...");

    // Initialize LCD device
    i2c_lcd_init_device();
    i2c_lcd_clear();
    i2c_lcd_backlight_set(false);

    while (1)
    {
        LOGI(TAG, "Update LCD...");
        i2c_lcd_backlight_set(true);

        //----- Display 1 - General info -----
        formatted_uptime(uptime, sizeof(uptime));
        formatted_datetime(formatted_time, sizeof(formatted_time));
        snprintf(buf, sizeof(buf), "%s\nUp: %s", formatted_time, uptime);
        i2c_lcd_write_text(buf); // The complete display contents is written in one time, not line by line
        vTaskDelay(pdMS_TO_TICKS(3000));

        //----- Display 2 - Network info -----
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            // Copy WiFi IP if present
            if (sm_actuals.wifi_ip_addr[0] != '\0')
                strncpy(wifi_ip, sm_actuals.wifi_ip_addr, sizeof(wifi_ip));

            // Copy Ethernet IP if present
            if (sm_actuals.eth_ip_addr[0] != '\0')
                strncpy(eth_ip, sm_actuals.eth_ip_addr, sizeof(eth_ip));

            xSemaphoreGive(statemachine_mutex);
        }

        if (ping_gateway())
        {
            snprintf(ping_result, sizeof(ping_result), "Gateway OK");
        }
        else
        {
            snprintf(ping_result, sizeof(ping_result), "Gateway unreachable");
        }

        snprintf(buf, sizeof(buf), "Wifi:%s\nEth:%s\n%s", wifi_ip, eth_ip, ping_result);
        i2c_lcd_write_text(buf); // The complete display contents is written in one time, not line by line
        vTaskDelay(pdMS_TO_TICKS(3000));

        //----- Display 2 - Statemachine state -----
        build_sm_actuals_buffer(buf, sizeof(buf));
        i2c_lcd_write_text(buf); // your LCD driver handles multiline
        vTaskDelay(pdMS_TO_TICKS(3000));

        //----- Display 3 - Sensors -----
        if (xQueuePeek(sensor_avg_queue, avg, 0) != pdTRUE)
        {
            LOGE(TAG, "Failed to read avg sensor queue");
            continue;
        }

        for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
        {
            for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
            {
                bool active = false;

                // Check if sensor is active (type != "None")
                if (xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
                {
                    const bus_sensor_t *meta = (bus == 0) ? &bus0_sensor_settings[ch] : &bus1_sensor_settings[ch];

                    active = (strcmp(meta->type, "None") != 0);
                    xSemaphoreGive(sensor_cfg_mutex);
                }

                if (!active)
                    continue; // skip inactive sensors

                // Format and send
                format_sensor_overview(buf, sizeof(buf), bus, ch, avg);
                i2c_lcd_write_text(buf);
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
        }

        //----- Display 4 - Valve positions -----
        build_valve_positions_buffer(buf, sizeof(buf));
        i2c_lcd_write_text(buf);
        vTaskDelay(pdMS_TO_TICKS(3000));

        /*float temp_c = 0;
        ds3231_health_monitor(ds3231_bus0_handle);
        ds3231_read_temperature(ds3231_bus0_handle, &temp_c);
        ds3231_check_status(ds3231_bus0_handle, ds3231_status, sizeof(ds3231_status));

        snprintf(lcd_contents, sizeof(lcd_contents), "DS3231 Temp: %.2f C%s", temp_c, ds3231_status);
        i2c_lcd_write_text(lcd_contents);*/

        // vTaskDelay(pdMS_TO_TICKS(10000));
        // i2c_lcd_clear();
        // i2c_lcd_backlight_set(false);
    }
}
