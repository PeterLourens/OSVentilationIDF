/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_eth.h"
#include "globals.h"

static const char *TAG = "task_eth";

/* Task storage for static creation */
static StackType_t eth_stack[4096];
static StaticTask_t eth_tcb;
static size_t eth_stack_size = 4096;

void start_task_eth(void)
{
    task_ethernet = xTaskCreateStatic(task_eth_code, "task_eth", eth_stack_size, NULL, 9, eth_stack, &eth_tcb);

    if (task_ethernet == NULL)
    {
        LOGE(TAG, "Failed to create Ethernet task");
    }
}

void task_eth_code(void *pvParameters)
{
    (void)pvParameters;
    LOGI(TAG, "Ethernet task starting");

    // All heavy lifting is in eth_manager_start()
    eth_manager_start();

    // Keep the task alive for diagnostics; event handlers do the work
    while (1)
    {
        if (!ethernet_config_is_enabled())
        {
            LOGI(TAG, "Ethernet disabled in config; skipping checks and stopping driver");
            eth_stop();
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        if (eth_is_connected())
        {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(eth_netif, &ip) == ESP_OK)
            {
                LOGI(TAG, "Ethernet Status: Connected, IP: " IPSTR, IP2STR(&ip.ip));
            }
        }
        else
        {
            LOGW(TAG, "Ethernet Status: Disconnected, attempting restart");
            esp_err_t err = esp_eth_start(eth_handle);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            {
                LOGE(TAG, "Ethernet restart failed: %s", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}