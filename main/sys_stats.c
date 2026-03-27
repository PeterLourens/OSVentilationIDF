/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "sys_stats.h"
#include "globals.h"

static const char *TAG = "Sys_stats";

// CPU Temperature
static temperature_sensor_handle_t temp_sensor = NULL;
static bool temp_sensor_initialized = false;

typedef struct
{
    char ip[16];
    char netmask[16];
    char gw[16];
    char dns1[16];
    char dns2[16];
    bool dhcp;
} EthSnapshot_t;

// Structure to hold ethernet snapshot
// static EthSnapshot_t eth_snapshot = {0};

// Safe append macro
#define SAFE_APPEND(fmt, ...)                                               \
    do                                                                      \
    {                                                                       \
        int _n = snprintf(out + used, buf_size - used, fmt, ##__VA_ARGS__); \
        if (_n < 0 || _n >= (int)(buf_size - used))                         \
        {                                                                   \
            used = buf_size - 1;                                            \
            out[used] = '\0';                                               \
            return used;                                                    \
        }                                                                   \
        used += _n;                                                         \
    } while (0)

// -- -Helpers-- -
static void sys_stats_init_temp_sensor(void)
{
    if (temp_sensor_initialized)
        return;

    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 80);

    esp_err_t err = temperature_sensor_install(&config, &temp_sensor);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Temp sensor install failed: %s", esp_err_to_name(err));
        return;
    }

    err = temperature_sensor_enable(temp_sensor);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Temp sensor enable failed: %s", esp_err_to_name(err));
        temperature_sensor_uninstall(temp_sensor);
        temp_sensor = NULL;
        return;
    }

    temp_sensor_initialized = true;
}

// Sorting tasks by CPU usage
static int compare_tasks(const void *a, const void *b)
{
    const TaskStatus_t *ta = a;
    const TaskStatus_t *tb = b;
    return (tb->ulRunTimeCounter - ta->ulRunTimeCounter);
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

const char *wifi_phy_to_str(int phy)
{
    switch (phy)
    {
    case 1:
        return "802.11b";
    case 2:
        return "802.11g";
    case 3:
        return "802.11n";
    default:
        return "unknown";
    }
}

const char *wifi_authmode_to_str(int auth)
{
    switch (auth)
    {
    case WIFI_AUTH_OPEN:
        return "Open";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3-PSK";
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI-PSK";
    default:
        return "unknown";
    }
}

static void update_cpu_temp(void)
{
    if (!temp_sensor_initialized)
        sys_stats_init_temp_sensor();

    if (!temp_sensor_initialized)
    {
        sm_actuals.cpu_temp = -1;
        return;
    }

    float temp_c = 0.0f;
    if (temperature_sensor_get_celsius(temp_sensor, &temp_c) == ESP_OK)
        sm_actuals.cpu_temp = (int)temp_c;
    else
        sm_actuals.cpu_temp = -1;
}

static void update_heap_stats(void)
{
    sm_actuals.heap_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    sm_actuals.heap_min_ever = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
}

static void update_stack_watermarks(void)
{
    sm_actuals.stack_i2c_sensors = uxTaskGetStackHighWaterMark(task_i2c_sensors);
    sm_actuals.stack_i2c_ds3231 = uxTaskGetStackHighWaterMark(task_i2c_ds3231);
    sm_actuals.stack_i2c_lcd = uxTaskGetStackHighWaterMark(task_i2c_lcd);
    sm_actuals.stack_rgb_leds = uxTaskGetStackHighWaterMark(task_rgb_leds);
    sm_actuals.stack_statemachine = uxTaskGetStackHighWaterMark(task_statemachine);
    sm_actuals.stack_valvectrl = uxTaskGetStackHighWaterMark(task_valvectrl);
    sm_actuals.stack_mqtt = uxTaskGetStackHighWaterMark(task_mqtt);
    sm_actuals.stack_influxdb = uxTaskGetStackHighWaterMark(task_influxdb);
    sm_actuals.stack_network_manager = uxTaskGetStackHighWaterMark(network_manager_task_handle);
}

static void update_wifi_stats(void)
{
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
        snprintf(sm_actuals.wifi_ssid, sizeof(sm_actuals.wifi_ssid), "%s", "00:00:00:00:00:00");
    }

    wifi_config_t cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK)
        snprintf(sm_actuals.wifi_ssid, sizeof(sm_actuals.wifi_ssid), "%s", (char *)cfg.sta.ssid);
    else
        snprintf(sm_actuals.wifi_ssid, sizeof(sm_actuals.wifi_ssid), "%s", "<unknown>");
}

static void update_wifi_info(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif)
    {
        LOGW(TAG, "WIFI_STA_DEF netif not found");
        return;
    }

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK)
    {
        esp_ip4addr_ntoa(&ip.ip, sm_actuals.wifi_ip_addr, sizeof(sm_actuals.wifi_ip_addr));
        esp_ip4addr_ntoa(&ip.netmask, sm_actuals.wifi_netmask, sizeof(sm_actuals.wifi_netmask));
        esp_ip4addr_ntoa(&ip.gw, sm_actuals.wifi_gateway, sizeof(sm_actuals.wifi_gateway));
    }

    // DNS
    esp_netif_dns_info_t dns;
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK)
        esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, sm_actuals.wifi_dns1, sizeof(sm_actuals.wifi_dns1));

    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK)
        esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, sm_actuals.wifi_dns2, sizeof(sm_actuals.wifi_dns2));
}

void sys_stats_update_actuals(void)
{
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return;

    // These are truly "live" stats and fine to recompute here
    update_cpu_temp();
    update_heap_stats();
    update_stack_watermarks();

    // Wi‑Fi: we still own stats + IP info here
    if (sm_actuals.wifi_connected)
    {
        update_wifi_stats();
        update_wifi_info();
    }
    else
    {
        // Wi‑Fi enabled but not connected → clear stats
        sm_actuals.wifi_rssi = -127;
        sm_actuals.wifi_channel = -1;
        sm_actuals.wifi_authmode = -1;
        sm_actuals.wifi_phy = 0;
        sm_actuals.wifi_bandwidth = 0;

        snprintf(sm_actuals.wifi_ssid, sizeof(sm_actuals.wifi_ssid), "%s", "00:00:00:00:00:00");
        snprintf(sm_actuals.wifi_ip_addr, sizeof(sm_actuals.wifi_ip_addr), "%s", "0.0.0.0");
        snprintf(sm_actuals.wifi_netmask, sizeof(sm_actuals.wifi_netmask), "%s", "0.0.0.0");
        snprintf(sm_actuals.wifi_gateway, sizeof(sm_actuals.wifi_gateway), "%s", "0.0.0.0");
        snprintf(sm_actuals.wifi_dns1, sizeof(sm_actuals.wifi_dns1), "%s", "0.0.0.0");
        snprintf(sm_actuals.wifi_dns2, sizeof(sm_actuals.wifi_dns2), "%s", "0.0.0.0");
    }

    xSemaphoreGive(statemachine_mutex);
}

static size_t append_header(char *out, size_t used, size_t buf_size)
{
    char uptime[64];
    char timestr[64];

    formatted_uptime(uptime, sizeof(uptime));
    formatted_daydatetime(timestr, sizeof(timestr));

    SAFE_APPEND("==== System snapshot ====\n\n");
    SAFE_APPEND("System time: %s\nUptime: %s\nReset reason: %s\n\n", timestr, uptime, reset_reason_str(esp_reset_reason()));

    return used;
}

static size_t append_task_table(char *out, size_t used, size_t buf_size)
{
    UBaseType_t count = uxTaskGetNumberOfTasks();

    if (count == 0)
    {
        SAFE_APPEND("No tasks\n\n");
        return used;
    }

    TaskStatus_t *tasks = pvPortMalloc(count * sizeof(TaskStatus_t));
    if (!tasks)
    {
        SAFE_APPEND("Out of memory getting tasks\n\n");
        return used;
    }

    uint32_t total = uxTaskGetSystemState(tasks, count, NULL);
    qsort(tasks, count, sizeof(TaskStatus_t), compare_tasks);

    SAFE_APPEND("%-20s %-4s %-4s %-12s %-12s %-6s\n", "Task Name", "St", "Prio", "HighWater", "RunTicks", "CPU%");
    SAFE_APPEND("%-20s %-4s %-4s %-12s %-12s %-6s\n", "--------------------", "--", "----", "------------", "------------", "------");

    for (UBaseType_t i = 0; i < count; i++)
    {
        TaskStatus_t *ts = &tasks[i];
        uint32_t hw = ts->usStackHighWaterMark * sizeof(StackType_t);
        float pct = total ? (100.0f * ts->ulRunTimeCounter) / total : 0.0f;

        SAFE_APPEND("%-20s %-4s %4u %12ld %12u %6.2f\n",
                    ts->pcTaskName ? ts->pcTaskName : "<no-name>",
                    state_char_from_enum(ts->eCurrentState),
                    (unsigned)ts->uxCurrentPriority,
                    hw,
                    (unsigned)ts->ulRunTimeCounter,
                    pct);
    }

    vPortFree(tasks);
    return used;
}

static size_t append_heap_stats(char *out, size_t used, size_t buf_size)
{
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    float frag = free_heap ? 100.0f * (1.0f - ((float)largest / free_heap)) : 0.0f;

    SAFE_APPEND("\nHeap metrics:\nfree_heap: %u\nmin_ever_free: %u\nlargest_free_block: %u\nHeap fragmentation estimate: %.1f%%\n", (unsigned)free_heap, (unsigned)min_free, (unsigned)largest, frag);

    return used;
}

static size_t append_system_stats(char *out, size_t used, size_t buf_size)
{
    Statemachine_actuals_t snapshot;

    // Take a short-lived lock and copy the global actuals into a local snapshot
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return used;

    // shallow copy; ensure sm_actuals_t contains no pointers
    snapshot = sm_actuals;
    xSemaphoreGive(statemachine_mutex);

    // Debug: log the snapshot we will print
    LOGD(TAG, "append_system_stats snapshot: wifi_enabled=%d wifi_connected=%d ethernet_enabled=%d eth_connected=%d\n",
         snapshot.wifi_enabled, snapshot.wifi_connected, snapshot.ethernet_enabled, snapshot.eth_connected);
    LOGD(TAG, "append_system_stats snapshot values: eth_ip=%s eth_netmask=%s eth_gw=%s wifi_ip=%s",
         snapshot.eth_ip_addr, snapshot.eth_netmask, snapshot.eth_gateway, snapshot.wifi_ip_addr);

    // Now format using the snapshot (no mutex held)
    SAFE_APPEND("\nHardware Stats:\nCPU Temp: %d C\n", snapshot.cpu_temp);

    if (snapshot.wifi_enabled || snapshot.wifi_connected)
    {
        SAFE_APPEND("\nWiFi Stats:\n"
                    "SSID: %s\nBSSID: %s\nRSSI: %d dBm\nChannel: %d\nPHY: %s\nBandwidth: %d MHz\nAuthmode: %s\n",
                    snapshot.wifi_ssid, snapshot.wifi_bssid, snapshot.wifi_rssi,
                    snapshot.wifi_channel, wifi_phy_to_str(snapshot.wifi_phy),
                    snapshot.wifi_bandwidth, wifi_authmode_to_str(snapshot.wifi_authmode));
    }

    if (snapshot.wifi_enabled || snapshot.wifi_connected)
    {
        SAFE_APPEND("\nWiFi Settings:\n"
                    "Hostname: %s\nIP: %s\nNetmask: %s\nGateway: %s\nDNS1: %s\nDNS2: %s\nDHCP: %s\n",
                    snapshot.hostname, snapshot.wifi_ip_addr, snapshot.wifi_netmask, snapshot.wifi_gateway,
                    snapshot.wifi_dns1, snapshot.wifi_dns2, snapshot.wifi_dhcp_enabled ? "enabled" : "static");
    }

    if (snapshot.ethernet_enabled || snapshot.eth_connected)
    {
        SAFE_APPEND("\nEthernet Settings:\n"
                    "Hostname: %s\nIP: %s\nNetmask: %s\nGateway: %s\nDNS1: %s\nDNS2: %s\nDHCP: %s\n\n",
                    snapshot.hostname, snapshot.eth_ip_addr, snapshot.eth_netmask, snapshot.eth_gateway,
                    snapshot.eth_dns1, snapshot.eth_dns2, snapshot.eth_dhcp_enabled ? "enabled" : "static");
    }
    else
    {
        LOGD(TAG, "Skipping Ethernet Settings in output: ethernet_enabled=%d eth_connected=%d",
             snapshot.ethernet_enabled, snapshot.eth_connected);
    }

    return used;
}

void show_system_and_task_stats(void)
{
    const size_t buf_size = 16 * 1024;
    char *out = malloc(buf_size);
    if (!out)
        return;

    size_t used = 0;

    used = append_header(out, used, buf_size);
    used = append_task_table(out, used, buf_size);
    used = append_heap_stats(out, used, buf_size);
    used = append_system_stats(out, used, buf_size);

    LOGD(TAG, "Sending sys_stats over websocket, %zu bytes", used);
    ws_async_send(out, used);
    free(out);
}
