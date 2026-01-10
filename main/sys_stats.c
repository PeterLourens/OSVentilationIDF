#include "sys_stats.h"

static const char *TAG = "Sys stats";

// -- -Helpers-- -

// CPU Temperature
static temperature_sensor_handle_t temp_sensor = NULL;
static bool temp_sensor_initialized = false;

static void sys_stats_init_temp_sensor(void)
{
    if (temp_sensor_initialized)
        return;

    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 80);

    esp_err_t err = temperature_sensor_install(&config, &temp_sensor);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Temp sensor install failed: %s", esp_err_to_name(err));
        return;
    }

    err = temperature_sensor_enable(temp_sensor);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Temp sensor enable failed: %s", esp_err_to_name(err));
        temperature_sensor_uninstall(temp_sensor);
        temp_sensor = NULL;
        return;
    }

    temp_sensor_initialized = true;
}

// Map task state enum to short string
const char *state_char_from_enum(eTaskState s)
{
    switch (s)
    {
    case eRunning:
        return "R ";
    case eReady:
        return "RD";
    case eBlocked:
        return "B ";
    case eSuspended:
        return "S ";
    case eDeleted:
        return "D ";
    default:
        return "? ";
    }
}

// Map reset reason to human‑friendly string
const char *reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return "Power‑on";
    case ESP_RST_EXT:
        return "External reset (reset pin)";
    case ESP_RST_SW:
        return "Software reset (esp_restart)";
    case ESP_RST_PANIC:
        return "Exception/panic";
    case ESP_RST_INT_WDT:
        return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "Task watchdog";
    case ESP_RST_WDT:
        return "Other watchdog";
    case ESP_RST_DEEPSLEEP:
        return "Wake from deep sleep";
    case ESP_RST_BROWNOUT:
        return "Brownout";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "Unknown";
    }
}

void sys_stats_update_actuals(void)
{
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return; // avoid blocking forever

    // --- CPU Temperature ---
    if (!temp_sensor_initialized)
    {
        sys_stats_init_temp_sensor();
    }

    if (temp_sensor_initialized)
    {
        float temp_c = 0.0f;
        esp_err_t terr = temperature_sensor_get_celsius(temp_sensor, &temp_c);
        if (terr == ESP_OK)
        {
            sm_actuals.cpu_temp = (int)temp_c; // or store float if you prefer
        }
        else
        {
            sm_actuals.cpu_temp = -1;
        }
    }
    else
    {
        sm_actuals.cpu_temp = -1;
    }

    // --- Heap Stats ---
    sm_actuals.heap_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    sm_actuals.heap_min_ever = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);

    // --- Stack High-Water Marks ---
    sm_actuals.stack_i2c_sensors = uxTaskGetStackHighWaterMark(task_i2c_sensors);
    sm_actuals.stack_i2c_ds3231 = uxTaskGetStackHighWaterMark(task_i2c_ds3231);
    sm_actuals.stack_wifi = uxTaskGetStackHighWaterMark(task_wifi);
    sm_actuals.stack_i2c_lcd = uxTaskGetStackHighWaterMark(task_i2c_lcd);
    sm_actuals.stack_rgb_leds = uxTaskGetStackHighWaterMark(task_rgb_leds);
    sm_actuals.stack_statemachine = uxTaskGetStackHighWaterMark(task_statemachine);
    sm_actuals.stack_valvectrl = uxTaskGetStackHighWaterMark(task_valvectrl);
    sm_actuals.stack_mqtt = uxTaskGetStackHighWaterMark(task_mqtt);
    sm_actuals.stack_influxdb = uxTaskGetStackHighWaterMark(task_influxdb);

    // --- WiFi AP info ---
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
        sm_actuals.wifi_rssi = ap.rssi;
        sm_actuals.wifi_channel = ap.primary;
        sm_actuals.wifi_authmode = ap.authmode;
        sm_actuals.wifi_phy = ap.phy_11b ? 1 : ap.phy_11g ? 2
                                           : ap.phy_11n   ? 3
                                                          : 0;
        sm_actuals.wifi_bandwidth = ap.second ? 40 : 20;

        snprintf(sm_actuals.wifi_bssid, sizeof(sm_actuals.wifi_bssid), "%02X:%02X:%02X:%02X:%02X:%02X", ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    }
    else
    {
        sm_actuals.wifi_rssi = -127;
        sm_actuals.wifi_channel = -1;
        sm_actuals.wifi_authmode = -1;
        sm_actuals.wifi_phy = 0;
        sm_actuals.wifi_bandwidth = 0;
        strcpy(sm_actuals.wifi_bssid, "00:00:00:00:00:00");
    }

    // --- WiFi config (SSID) ---
    wifi_config_t cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK)
    {
        snprintf(sm_actuals.wifi_ssid, sizeof(sm_actuals.wifi_ssid), "%s", (char *)cfg.sta.ssid);
    }
    else
    {
        strcpy(sm_actuals.wifi_ssid, "<unknown>");
    }

    // --- Network interface info ---
    esp_netif_ip_info_t ip;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK)
    {
        esp_ip4addr_ntoa(&ip.ip, sm_actuals.ip_addr, sizeof(sm_actuals.ip_addr));
        esp_ip4addr_ntoa(&ip.netmask, sm_actuals.netmask, sizeof(sm_actuals.netmask));
        esp_ip4addr_ntoa(&ip.gw, sm_actuals.gateway, sizeof(sm_actuals.gateway));
    }

    // DNS
    esp_netif_dns_info_t dns;
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK)
        esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, sm_actuals.dns1, sizeof(sm_actuals.dns1));

    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK)
        esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, sm_actuals.dns2, sizeof(sm_actuals.dns2));

    // DHCP enabled?
    sm_actuals.dhcp_enabled = esp_netif_is_netif_up(netif);

    xSemaphoreGive(statemachine_mutex);
}

void show_system_and_task_stats(void)
{
    const size_t SNAPSHOT_BUF_SIZE = 32 * 1024;
    char *out = malloc(SNAPSHOT_BUF_SIZE);
    if (!out)
        return;
    size_t used = 0;

    // Update globals stats
    sys_stats_update_actuals();

    // Header
    // TickType_t ticks = xTaskGetTickCount();
    // uint64_t ms = (uint64_t)ticks * portTICK_PERIOD_MS;

    char uptime_str[64] = {};
    formatted_uptime(uptime_str, sizeof(uptime_str));
    ESP_LOGI(TAG, "System uptime: %s", uptime_str);

    char time_str[64] = {};
    formatted_daydatetime(time_str, sizeof(time_str));
    ESP_LOGI(TAG, "System time: %s", time_str);

    esp_reset_reason_t rr = esp_reset_reason();

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "==== System snapshot ====\n\n");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "System time: %s\nUptime: %s \nReset reason: %s\n\n", time_str, uptime_str, reset_reason_str(rr));

    // Task runtime table
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count == 0)
    {
        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "No tasks\n\n");
        goto send_out;
    }

    TaskStatus_t *tasks = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (!tasks)
    {
        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "Out of memory getting tasks\n\n");
        goto send_out;
    }

    uint32_t total_run_time = uxTaskGetSystemState(tasks, task_count, NULL);

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %-4s %-4s %-12s %-12s %-6s\n",
                     "Task Name", "St", "Prio", "HighWater", "RunTicks", "CPU%");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %-4s %-4s %-12s %-12s %-6s\n",
                     "--------------------", "--", "----", "------------", "------------", "------");

    for (UBaseType_t i = 0; i < task_count; i++)
    {
        TaskStatus_t *ts = &tasks[i];
        uint32_t highwater_bytes = ts->usStackHighWaterMark * sizeof(StackType_t);
        float pct = (total_run_time > 0) ? (100.0f * ts->ulRunTimeCounter) / (float)total_run_time : 0.0f;

        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                         "%-20s %-4s %4u %12u %12u %6.2f\n",
                         ts->pcTaskName ? ts->pcTaskName : "<no-name>",
                         state_char_from_enum(ts->eCurrentState),
                         (unsigned)ts->uxCurrentPriority,
                         (unsigned)highwater_bytes,
                         (unsigned)ts->ulRunTimeCounter,
                         pct);
    }

    // Heap metrics
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    float frag_pct = free_heap ? 100.0f * (1.0f - ((float)largest / (float)free_heap)) : 0.0f;

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "\nHeap metrics:\n"
                     "free_heap: %u\nmin_ever_free: %u\nlargest_free_block: %u\n"
                     "Heap fragmentation estimate: %.1f%%\n",
                     (unsigned)free_heap, (unsigned)min_free, (unsigned)largest, frag_pct);

    // Other stats
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return;

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "\nHardware Stats:\n"
                     "CPU Temp: %d C\n",
                     sm_actuals.cpu_temp);

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "\nWiFi Stats:\n"
                     "SSID: %s\n"
                     "BSSID: %s\n"
                     "RSSI: %d dBm\n"
                     "Channel: %d\n"
                     "PHY: %d\n"
                     "Bandwidth: %d MHz\n"
                     "Authmode: %d\n\n",
                     sm_actuals.wifi_ssid,
                     sm_actuals.wifi_bssid,
                     sm_actuals.wifi_rssi,
                     sm_actuals.wifi_channel,
                     sm_actuals.wifi_phy,
                     sm_actuals.wifi_bandwidth,
                     sm_actuals.wifi_authmode);

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "Network Settings:\n"
                     "IP: %s\n"
                     "Netmask: %s\n"
                     "Gateway: %s\n"
                     "DNS1: %s\n"
                     "DNS2: %s\n"
                     "DHCP: %s\n\n",
                     sm_actuals.ip_addr,
                     sm_actuals.netmask,
                     sm_actuals.gateway,
                     sm_actuals.dns1,
                     sm_actuals.dns2,
                     sm_actuals.dhcp_enabled ? "enabled" : "static");
    xSemaphoreGive(statemachine_mutex);

    // Network information
    // wifi_config_t wifi_cfg;
    // esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
    // const char *ssid = (const char *)wifi_cfg.sta.ssid;

    /*used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "\n\nNetwork Settings:\n"
                     "┌──────────────┬────────────────────────────────┐\n"
                     "│ Setting      │ Value                          │\n"
                     "├──────────────┼────────────────────────────────┤\n"
                     "│ DHCP         │ %-30s │\n"
                     "│ SSID         │ %-30s │\n"
                     "| Wifi Pass.   | %-30s │\n"
                     "│ IP Address   │ %-30s │\n"
                     "│ Subnet Mask  │ %-30s │\n"
                     "│ Gateway      │ %-30s │\n"
                     "│ Primary DNS  │ %-30s │\n"
                     "│ Secondary DNS│ %-30s │\n"
                     "| Time Server  │ %-30s │\n"
                     "| Timezone     │ %-30s │\n"
                     "└──────────────┴────────────────────────────────┘",
                     network_config.enable_dhcp, ssid, network_config.wifi_password, network_config.ip_address,
                     network_config.subnet_mask, network_config.gateway, network_config.primary_dns, network_config.secondary_dns,
                     time_config.ntp_server, time_config.timezone);*/

send_out:
    // Print to UART
    // printf("%s", out);

    // Send to WebSocket clients
    // ws_async_send("STATS:\n", strlen("STATS:\n"));
    ws_async_send(out, used);

    free(out);
}
