/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_i2c_lcd.c
 * @brief Task implementation for LCD updates and periodic display refresh.
 */

#include <esp_attr.h>
#include "task_i2c_lcd.h"

static const char *TAG = "task_i2c_LCD";

EXT_RAM_BSS_ATTR static StackType_t i2c_lcd_stack[5120]; // stack buffer
static StaticTask_t i2c_lcd_tcb;                         // TCB buffer
static size_t i2c_lcd_stack_size = 5120;                 // 5120 words = 20 KB
static volatile bool i2c_lcd_stop_requested = false;

static bool i2c_lcd_task_enabled(void)
{
    bool enabled = false;

    if (i2c_config_mutex && xSemaphoreTake(i2c_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        enabled = strcmp(i2c_config.enable_lcd, "On") == 0;
        xSemaphoreGive(i2c_config_mutex);
    }

    return enabled;
}

static void i2c_lcd_delay(TickType_t ticks)
{
    while (ticks > 0 && !i2c_lcd_stop_requested && i2c_lcd_task_enabled())
    {
        TickType_t slice = ticks > pdMS_TO_TICKS(100) ? pdMS_TO_TICKS(100) : ticks;
        vTaskDelay(slice);
        ticks -= slice;
    }
}

void stop_task_i2c_lcd(void)
{
    i2c_lcd_stop_requested = true;

    if (task_i2c_lcd == NULL)
    {
        LOGI(TAG, "LCD task already stopped");
        return;
    }

    LOGI(TAG, "Stopping LCD task...");
    TickType_t start = xTaskGetTickCount();
    while (task_i2c_lcd != NULL && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(5000))
    {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void start_task_i2c_lcd(void)
{
    if (!i2c_lcd_task_enabled())
    {
        LOGI(TAG, "LCD disabled → task not started");
        return;
    }

    if (task_i2c_lcd != NULL)
    {
        LOGW(TAG, "LCD task already running");
        return;
    }

    i2c_lcd_stop_requested = false;
    task_i2c_lcd = xTaskCreateStatic(task_i2c_lcd_code, "i2c_lcd", i2c_lcd_stack_size, NULL, 6, i2c_lcd_stack, &i2c_lcd_tcb);

    if (!task_i2c_lcd)
        LOGE(TAG, "LCD task creation failed");
    else
        LOGI(TAG, "LCD task started");
}

void task_i2c_lcd_code(void *pvParameters)
{
    char uptime[20] = {};
    char ping_result[20] = {};
    char formatted_time[32] = {};
    char wifi_ip[16] = "0.0.0.0";
    char eth_ip[16] = "0.0.0.0";
    char buf[200] = {};

    if (!i2c_lcd_task_enabled())
    {
        LOGW(TAG, "LCD disabled → terminating LCD task");
        task_i2c_lcd = NULL;
        vTaskDelete(NULL);
    }

    // LCD availability flag (degraded mode)
    bool lcd_available = false;

    // Try to initialize LCD device with retries and backoff.
    const TickType_t retry_initial_delay = pdMS_TO_TICKS(1000);
    const TickType_t retry_long_delay = pdMS_TO_TICKS(30000);

    for (;;)
    {
        if (i2c_lcd_stop_requested || !i2c_lcd_task_enabled())
        {
            LOGI(TAG, "LCD disabled by config; task exiting");
            i2c_lcd_clear();
            i2c_lcd_backlight_set(false);
            task_i2c_lcd = NULL;
            vTaskDelete(NULL);
        }

        if (!lcd_available)
        {
            if (i2c_lcd_init_device() == ESP_OK)
            {
                lcd_available = true;
                LOGI(TAG, "LCD device initialized and available");
                // Ensure display is in known state
                i2c_lcd_clear();
                i2c_lcd_backlight_set(false);
            }
            else
            {
                LOGW(TAG, "LCD init failed; will retry in 1s");
                i2c_lcd_delay(retry_initial_delay);
                // Try again a few times quickly, then back off
                if (i2c_lcd_init_device() != ESP_OK)
                {
                    LOGW(TAG, "LCD init retry failed; backing off 30s");
                    i2c_lcd_delay(retry_long_delay);
                    continue;
                }
                else
                {
                    lcd_available = true;
                    LOGI(TAG, "LCD device initialized on retry");
                    i2c_lcd_clear();
                    i2c_lcd_backlight_set(false);
                }
            }
        }

        // If LCD is available, perform normal update cycle
        if (lcd_available)
        {
            // Display 1 - General info
            formatted_uptime(uptime, sizeof(uptime));
            formatted_datetime(formatted_time, sizeof(formatted_time));
            snprintf(buf, sizeof(buf), "%s\nUp: %s", formatted_time, uptime);
            if (i2c_lcd_write_text(buf) != ESP_OK)
            {
                LOGW(TAG, "Failed to write general info to LCD; marking LCD unavailable");
                lcd_available = false;
                // small delay before next init attempt
                i2c_lcd_delay(pdMS_TO_TICKS(500));
                continue;
            }
            i2c_lcd_delay(pdMS_TO_TICKS(3000));

            // Display 2 - Network info
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
            {
                if (sm_actuals.wifi_ip_addr[0] != '\0')
                    strncpy(wifi_ip, sm_actuals.wifi_ip_addr, sizeof(wifi_ip) - 1);
                if (sm_actuals.eth_ip_addr[0] != '\0' && hw.has_ethernet)
                    strncpy(eth_ip, sm_actuals.eth_ip_addr, sizeof(eth_ip) - 1);
                xSemaphoreGive(statemachine_mutex);
            }
            else
            {
                LOGD(TAG, "statemachine_mutex not available for network info");
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
            if (i2c_lcd_write_text(buf) != ESP_OK)
            {
                LOGW(TAG, "Failed to write network info to LCD; marking LCD unavailable");
                lcd_available = false;
                i2c_lcd_delay(pdMS_TO_TICKS(500));
                continue;
            }
            i2c_lcd_delay(pdMS_TO_TICKS(3000));

            // Display 3 - Statemachine state
            build_sm_actuals_buffer(buf, sizeof(buf));
            if (i2c_lcd_write_text(buf) != ESP_OK)
            {
                LOGW(TAG, "Failed to write state info to LCD; marking LCD unavailable");
                lcd_available = false;
                i2c_lcd_delay(pdMS_TO_TICKS(500));
                continue;
            }
            i2c_lcd_delay(pdMS_TO_TICKS(3000));

            // Display 4 - Sensors
            // Wait for sensor queues to be ready
            while (!sensors_queues_ready && !i2c_lcd_stop_requested && i2c_lcd_task_enabled())
            {
                LOGD(TAG, "Waiting for sensor queues to be ready...");
                i2c_lcd_delay(pdMS_TO_TICKS(50));
            }

            if (sensor_avg_item_size != sizeof(float[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]))
            {
                LOGE(TAG, "sensor_avg_queue item size mismatch: expected %zu got %zu",
                     sizeof(float[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]), sensor_avg_item_size);
                // skip sensor display but keep LCD alive
            }
            else
            {
                float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
                if (xQueuePeek(sensor_avg_queue, avg, 0) != pdTRUE)
                {
                    LOGW(TAG, "Failed to read avg sensor queue; skipping sensor display");
                }
                else
                {
                    for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
                    {
                        for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
                        {
                            bool active = false;
                            if (sensor_cfg_mutex && xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
                            {
                                const bus_sensor_t *meta = (bus == 0) ? &bus0_sensor_settings[ch] : &bus1_sensor_settings[ch];
                                active = (strcmp(meta->type, "None") != 0);
                                xSemaphoreGive(sensor_cfg_mutex);
                            }
                            else
                            {
                                LOGD(TAG, "sensor_cfg_mutex not available; assuming sensor active");
                                active = true;
                            }

                            if (!active)
                                continue;

                            format_sensor_overview(buf, sizeof(buf), bus, ch, avg);
                            if (i2c_lcd_write_text(buf) != ESP_OK)
                            {
                                LOGW(TAG, "Failed to write sensor overview for b%ds%d; marking LCD unavailable", bus, ch);
                                lcd_available = false;
                                break;
                            }
                            i2c_lcd_delay(pdMS_TO_TICKS(3000));
                        }
                        if (!lcd_available)
                            break;
                    }
                }
            }

            if (!lcd_available)
            {
                // short delay before re-init attempts
                i2c_lcd_delay(pdMS_TO_TICKS(500));
                continue;
            }

            // Display 5 - Valve positions
            build_valve_positions_buffer(buf, sizeof(buf));
            if (i2c_lcd_write_text(buf) != ESP_OK)
            {
                LOGW(TAG, "Failed to write valve positions to LCD; marking LCD unavailable");
                lcd_available = false;
                i2c_lcd_delay(pdMS_TO_TICKS(500));
                continue;
            }
            i2c_lcd_delay(pdMS_TO_TICKS(3000));
        }
        else
        {
            // LCD not available: wait and retry init periodically
            LOGI(TAG, "LCD unavailable; retrying initialization in 30s");
            i2c_lcd_delay(retry_long_delay);
            // next loop iteration will attempt init
        }
    }
}
