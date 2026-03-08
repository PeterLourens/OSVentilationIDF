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

static void update_ethernet_info(void)
{
    /* Local scratch buffers to avoid holding mutex while calling esp-netif APIs */
    char ipbuf[16] = "0.0.0.0";
    char nm[16] = "0.0.0.0";
    char gw[16] = "0.0.0.0";
    char dns1[16] = "0.0.0.0";
    char dns2[16] = "0.0.0.0";
    bool dhcp = false;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (!netif)
    {
        LOGW(TAG, "ETH_DEF netif not found; will write defaults into actuals");
        /* fall through to update sm_actuals with defaults below */
    }
    else
    {
        LOGI(TAG, "ETH_DEF netif handle: %p", (void *)netif);

        /* Query IP info once */
        esp_netif_ip_info_t ip;
        esp_err_t err = esp_netif_get_ip_info(netif, &ip);
        if (err == ESP_OK && ip.ip.addr != 0)
        {
            esp_ip4addr_ntoa(&ip.ip, ipbuf, sizeof(ipbuf));
            esp_ip4addr_ntoa(&ip.netmask, nm, sizeof(nm));
            esp_ip4addr_ntoa(&ip.gw, gw, sizeof(gw));
            LOGI(TAG, "Got ETH IP: %s netmask: %s gw: %s", ipbuf, nm, gw);
        }
        else
        {
            LOGW(TAG, "esp_netif_get_ip_info failed or no IP assigned: %s (0x%08X)",
                 esp_err_to_name(err), err);
            /* ipbuf/nm/gw remain "0.0.0.0" */
        }

        /* DNS main */
        esp_netif_dns_info_t dns;
        err = esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
        if (err == ESP_OK)
        {
            esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, dns1, sizeof(dns1));
            LOGI(TAG, "ETH DNS1: %s", dns1);
        }
        else
        {
            LOGW(TAG, "esp_netif_get_dns_info (MAIN) failed: %s (0x%08X)", esp_err_to_name(err), err);
        }

        /* DNS backup */
        err = esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
        if (err == ESP_OK)
        {
            esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, dns2, sizeof(dns2));
            LOGI(TAG, "ETH DNS2: %s", dns2);
        }
        else
        {
            LOGW(TAG, "esp_netif_get_dns_info (BACKUP) failed: %s (0x%08X)", esp_err_to_name(err), err);
        }

        /* DHCP state (helper may take its own mutex) */
        dhcp = ethernet_config_is_dhcp_enabled();
    }

    /* Now update the global actuals under the statemachine mutex in one shot.
       Try a few times with short delays so we don't silently skip the update and leave empty strings. */
    const TickType_t try_delay = pdMS_TO_TICKS(20);
    const int max_attempts = 5;
    int attempt;
    bool updated = false;

    for (attempt = 0; attempt < max_attempts; ++attempt)
    {
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, 0) == pdTRUE)
        {
            /* Copy the local scratch buffers into the global actuals */
            snprintf(sm_actuals.eth_ip_addr, sizeof(sm_actuals.eth_ip_addr), "%s", ipbuf);
            snprintf(sm_actuals.eth_netmask, sizeof(sm_actuals.eth_netmask), "%s", nm);
            snprintf(sm_actuals.eth_gateway, sizeof(sm_actuals.eth_gateway), "%s", gw);
            snprintf(sm_actuals.eth_dns1, sizeof(sm_actuals.eth_dns1), "%s", dns1);
            snprintf(sm_actuals.eth_dns2, sizeof(sm_actuals.eth_dns2), "%s", dns2);
            sm_actuals.eth_dhcp_enabled = dhcp;
            xSemaphoreGive(statemachine_mutex);
            updated = true;
            break;
        }

        /* If we couldn't take the mutex immediately, wait a bit and retry */
        vTaskDelay(try_delay);
    }

    if (!updated)
    {
        /* Last-ditch attempt with a blocking take (short timeout) to avoid leaving empty fields */
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            snprintf(sm_actuals.eth_ip_addr, sizeof(sm_actuals.eth_ip_addr), "%s", ipbuf);
            snprintf(sm_actuals.eth_netmask, sizeof(sm_actuals.eth_netmask), "%s", nm);
            snprintf(sm_actuals.eth_gateway, sizeof(sm_actuals.eth_gateway), "%s", gw);
            snprintf(sm_actuals.eth_dns1, sizeof(sm_actuals.eth_dns1), "%s", dns1);
            snprintf(sm_actuals.eth_dns2, sizeof(sm_actuals.eth_dns2), "%s", dns2);
            sm_actuals.eth_dhcp_enabled = dhcp;
            xSemaphoreGive(statemachine_mutex);
            updated = true;
        }
    }

    if (!updated)
    {
        LOGW(TAG, "update_ethernet_info: failed to take statemachine_mutex to update actuals (will retry later)");
    }

    /* Extra debug: if netif exists, check whether it reports an IP (useful if esp_netif_get_ip_info succeeded but IP==0) */
    if (netif)
    {
        esp_netif_ip_info_t ip_check;
        if (esp_netif_get_ip_info(netif, &ip_check) == ESP_OK && ip_check.ip.addr == 0)
        {
            LOGW(TAG, "ETH netif has no IP assigned (ip.addr == 0)");
        }
    }

    LOGI(TAG, "Update actuals: Ethernet IP: %s, DNS1: %s, DNS2: %s, DHCP: %s",
         sm_actuals.eth_ip_addr, sm_actuals.eth_dns1, sm_actuals.eth_dns2, sm_actuals.eth_dhcp_enabled ? "enabled" : "static");
}

void sys_stats_update_actuals(void)
{
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return;

    update_cpu_temp();
    update_heap_stats();
    update_stack_watermarks();
    // update_wifi_stats();
    // update_wifi_info();
    // update_ethernet_info();

    if (sm_actuals.wifi_connected)
    {
        update_wifi_stats();
        update_wifi_info();
    }
    else
    {
        // Wi-Fi enabled but not connected → clear stats
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

    bool eth_connected_now = eth_is_connected();
    // bool eth_has_ip_now = network_has_ip();

    if (eth_connected_now)
    {
        // If the link is up, attempt to refresh the ethernet info (update_ethernet_info will handle missing IPs and write defaults).
        update_ethernet_info();
    }
    else
    {
        /* only clear when interface is definitely down / no IP */
        snprintf(sm_actuals.eth_ip_addr, sizeof(sm_actuals.eth_ip_addr), "%s", "0.0.0.0");
        snprintf(sm_actuals.eth_netmask, sizeof(sm_actuals.eth_netmask), "%s", "0.0.0.0");
        snprintf(sm_actuals.eth_gateway, sizeof(sm_actuals.eth_gateway), "%s", "0.0.0.0");
        snprintf(sm_actuals.eth_dns1, sizeof(sm_actuals.eth_dns1), "%s", "0.0.0.0");
        snprintf(sm_actuals.eth_dns2, sizeof(sm_actuals.eth_dns2), "%s", "0.0.0.0");
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

/*static size_t append_system_stats(char *out, size_t used, size_t buf_size)
{
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return used;

    // Log the deciding flags and a quick snapshot
    LOGI(TAG, "append_system_stats: wifi_enabled=%d wifi_connected=%d ethernet_enabled=%d eth_connected=%d",
         sm_actuals.wifi_enabled, sm_actuals.wifi_connected, sm_actuals.ethernet_enabled, sm_actuals.eth_connected);

    LOGI(TAG, "append_system_stats snapshot: eth_ip=%s eth_netmask=%s eth_gw=%s wifi_ip=%s",
         sm_actuals.eth_ip_addr, sm_actuals.eth_netmask, sm_actuals.eth_gateway, sm_actuals.wifi_ip_addr);

    SAFE_APPEND("\nHardware Stats:\nCPU Temp: %d C\n", sm_actuals.cpu_temp);

    // Wi-Fi section (only if enabled or connected)
    if (sm_actuals.wifi_enabled || sm_actuals.wifi_connected)
    {
        SAFE_APPEND("\nWiFi Stats:\n"
                    "SSID: %s\nBSSID: %s\nRSSI: %d dBm\nChannel: %d\nPHY: %s\nBandwidth: %d MHz\nAuthmode: %s\n",
                    sm_actuals.wifi_ssid, sm_actuals.wifi_bssid, sm_actuals.wifi_rssi,
                    sm_actuals.wifi_channel, wifi_phy_to_str(sm_actuals.wifi_phy),
                    sm_actuals.wifi_bandwidth, wifi_authmode_to_str(sm_actuals.wifi_authmode));
    }

    // Wi-Fi network info (only if enabled or connected)
    if (sm_actuals.wifi_enabled || sm_actuals.wifi_connected)
    {
        SAFE_APPEND("\nWiFi Settings:\n"
                    "Hostname: %s\nIP: %s\nNetmask: %s\nGateway: %s\nDNS1: %s\nDNS2: %s\nDHCP: %s\n",
                    sm_actuals.hostname, sm_actuals.wifi_ip_addr, sm_actuals.wifi_netmask, sm_actuals.wifi_gateway, sm_actuals.wifi_dns1, sm_actuals.wifi_dns2, sm_actuals.wifi_dhcp_enabled ? "enabled" : "static");
    }

    // Ethernet section (only if enabled or connected)
    if (sm_actuals.ethernet_enabled || sm_actuals.eth_connected)
    {
        SAFE_APPEND("\nEthernet Settings:\n"
                    "Hostname: %s\nIP: %s\nNetmask: %s\nGateway: %s\nDNS1: %s\nDNS2: %s\nDHCP: %s\n\n",
                    sm_actuals.hostname, sm_actuals.eth_ip_addr, sm_actuals.eth_netmask, sm_actuals.eth_gateway, sm_actuals.eth_dns1, sm_actuals.eth_dns2, sm_actuals.eth_dhcp_enabled ? "enabled" : "static");
    }

    xSemaphoreGive(statemachine_mutex);
    return used;
}*/

static size_t append_system_stats(char *out, size_t used, size_t buf_size)
{
    Statemachine_actuals_t snapshot;

    // Take a short-lived lock and copy the global actuals into a local snapshot
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return used;

    snapshot = sm_actuals; // shallow copy; ensure sm_actuals_t contains no pointers
    xSemaphoreGive(statemachine_mutex);

    // Debug: log the snapshot we will print
    LOGI(TAG, "append_system_stats snapshot: wifi_enabled=%d wifi_connected=%d ethernet_enabled=%d eth_connected=%d\n",
         snapshot.wifi_enabled, snapshot.wifi_connected, snapshot.ethernet_enabled, snapshot.eth_connected);
    LOGI(TAG, "append_system_stats snapshot values: eth_ip=%s eth_netmask=%s eth_gw=%s wifi_ip=%s",
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

    // Update is done globally
    // sys_stats_update_actuals();

    used = append_header(out, used, buf_size);
    used = append_task_table(out, used, buf_size);
    used = append_heap_stats(out, used, buf_size);
    used = append_system_stats(out, used, buf_size);

    ESP_LOGD(TAG, "Sending sys_stats over websocket, %zu bytes", used);
    ws_async_send(out, used);
    free(out);
}
