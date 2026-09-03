/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file network_manager.c
 * @brief Network manager task implementation for Ethernet/Wi-Fi lifecycle and watchdog behavior.
 */

#include <esp_attr.h>
#include "network_manager.h"

static const char *TAG = "net_mgr";

typedef struct
{
    bool enabled_cfg;                    // Ethernet enabled in settings
    bool link_up_event;                  // ETHERNET_EVENT_CONNECTED seen
    bool has_ip_event;                   // IP_EVENT_ETH_GOT_IP seen
    TickType_t last_state_change;        // last transition
    TickType_t last_watchdog_run;        // last tick
    int gateway_fail_count;              // consecutive gateway failures
    TickType_t last_successful_activity; // last time we saw a successful L2/L3 exchange
    TickType_t last_gateway_probe;       // last time we probed gateway successfully
    TickType_t last_restart_time;        // last time watchdog performed a restart (for grace period)
} eth_watchdog_t;

static eth_watchdog_t s_eth_wd = {0};

// Add to eth_watchdog_t
//TickType_t last_successful_activity; // last time we saw a successful L2/L3 exchange
//TickType_t last_gateway_probe;       // last time we probed gateway successfully

// Forward declarations
static void eth_watchdog_tick(void);
static void eth_watchdog_force_restart(const char *reason);

// Task handle and stack
EXT_RAM_BSS_ATTR static StackType_t network_manager_stack[12288]; // stack buffer
static StaticTask_t network_manager_tcb;
static size_t network_manager_stack_size = 12288; // 12 KiB stack for heavy network recovery work

// Event handler instances
static esp_event_handler_instance_t wifi_evt_inst = NULL;
static esp_event_handler_instance_t ip_evt_inst = NULL;
static esp_event_handler_instance_t eth_evt_inst = NULL;
static esp_event_handler_instance_t mqtt_evt_inst = NULL;

// Manager state
static volatile bool manager_running = false;

// Request flags (set by event handlers, acted on in main loop)
static volatile bool s_request_mqtt_restart = false;
static volatile bool s_request_ha_discovery = false;
static volatile bool s_discovery_requested = false;
static volatile bool eth_shutting_down = false;
static volatile bool s_request_syslog_update = false;
static volatile int s_eth_restart_attempts = 0;

static volatile bool s_request_eth_set_default = false; // set by eth event, acted on by manager
static volatile bool s_request_update_actuals = false;  // request to refresh sm_actuals from netifs

// Forward declarations
static void eth_watchdog_tick(void);
static void eth_watchdog_force_restart(const char *reason);
static bool eth_watchdog_gateway_reachable(void);

static bool safe_eth_ip_info(esp_netif_ip_info_t *ip)
{
    if (!ip || eth_is_shutting_down())
        return false;

    if (eth_handle == NULL)
        return false;

    esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (!eth)
        return false;

    return esp_netif_get_ip_info(eth, ip) == ESP_OK;
}

static void eth_watchdog_force_restart(const char *reason)
{
    static int software_fail_count = 0;
    static TickType_t last_attempt_tick = 0;

    ESP_LOGW(TAG, "ETH-WD: forcing Ethernet restart (%s)", reason);

    s_eth_wd.last_successful_activity = 0;
    s_eth_wd.last_gateway_probe = 0;
    s_request_update_actuals = true;
    s_request_syslog_update = true;

    // Optional: decay the failure counter if last attempt was long ago.
    if (last_attempt_tick != 0)
    {
        TickType_t now = xTaskGetTickCount();
        if ((now - last_attempt_tick) > pdMS_TO_TICKS(SOFTWARE_RESTART_DECAY_MS))
        {
            software_fail_count = 0;
        }
    }

    last_attempt_tick = xTaskGetTickCount();

    // Prefer a lighter software restart first, and only use hardware reset after retries.
    esp_err_t err = eth_software_restart();
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "ETH-WD: software restart succeeded");
        software_fail_count = 0;
        s_eth_wd.link_up_event = false;
        s_eth_wd.has_ip_event = false;
        s_eth_wd.gateway_fail_count = 0;
        s_eth_wd.last_state_change = xTaskGetTickCount();
        s_eth_wd.last_restart_time = xTaskGetTickCount(); // record restart time for grace period
        s_request_update_actuals = true;
        s_request_syslog_update = true;
        last_attempt_tick = 0;
        return;
    }

    ESP_LOGW(TAG, "ETH-WD: software restart failed: %s", esp_err_to_name(err));
    software_fail_count++;
    ESP_LOGW(TAG, "ETH-WD: software restart failure count = %d", software_fail_count);
    if (software_fail_count < SOFTWARE_RESTART_MAX_ATTEMPTS)
    {
        ESP_LOGW(TAG, "ETH-WD: not yet at hardware reset threshold (%d/%d); deferring hardware reset", software_fail_count, SOFTWARE_RESTART_MAX_ATTEMPTS);
        return;
    }

    ESP_LOGW(TAG, "ETH-WD: reached %d software failures; performing hardware reset", SOFTWARE_RESTART_MAX_ATTEMPTS);

    // esp_err_t hr = eth_hardware_reset();
    esp_err_t hr = eth_hardware_reset_safe();
    if (hr == ESP_OK)
    {
        ESP_LOGI(TAG, "ETH-WD: hardware reset completed");
        software_fail_count = 0;
        s_eth_wd.link_up_event = false;
        s_eth_wd.has_ip_event = false;
        s_eth_wd.gateway_fail_count = 0;
        s_eth_wd.last_state_change = xTaskGetTickCount();
        s_eth_wd.last_restart_time = xTaskGetTickCount(); // record restart time for grace period
        s_request_update_actuals = true;
        s_request_syslog_update = true;
        last_attempt_tick = 0;
        return;
    }

    ESP_LOGW(TAG, "ETH-WD: hardware reset failed: %s", esp_err_to_name(hr));
    s_eth_wd.last_state_change = xTaskGetTickCount();
    s_request_update_actuals = true;
    s_request_syslog_update = true;
}

static bool eth_watchdog_gateway_reachable(void)
{
    esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (!eth)
        return false;

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(eth, &ip) != ESP_OK)
        return false;
    if (ip.gw.addr == 0)
        return false;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return false;

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = htons(80),
        .sin_addr.s_addr = ip.gw.addr};

    struct timeval tv = {.tv_sec = 0, .tv_usec = 1000}; // 1 ms
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int r = connect(s, (struct sockaddr *)&sa, sizeof(sa));
    close(s);

    bool ok = (r == 0 || errno == EINPROGRESS);
    if (ok)
    {
        s_eth_wd.last_gateway_probe = xTaskGetTickCount();
        // Also treat gateway reachability as activity
        s_eth_wd.last_successful_activity = s_eth_wd.last_gateway_probe;
    }
    return ok;
}

static void log_network_status(void)
{
    esp_netif_t *active = get_active_netif();
    if (!active)
    {
        LOGI(TAG, "Network manager: no active network interface is currently selected; network stack is offline");
        return;
    }

    const char *active_if = "unknown";
    if (active == esp_netif_get_handle_from_ifkey("ETH_DEF"))
        active_if = "ETH_DEF";
    else if (active == esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"))
        active_if = "WIFI_STA_DEF";
    else if (active == esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"))
        active_if = "WIFI_AP_DEF";

    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(active, &ip_info) == ESP_OK && ip_info.ip.addr != 0)
    {
        LOGI(TAG, "Network manager: active interface %s has IP " IPSTR,
             active_if, IP2STR(&ip_info.ip));
    }
    else
    {
        LOGI(TAG, "Network manager: active interface %s has no IP",
             active_if);
    }
}

static void eth_watchdog_tick(void)
{
    TickType_t now = xTaskGetTickCount();

    if (s_eth_wd.last_watchdog_run != 0 &&
        (now - s_eth_wd.last_watchdog_run) < pdMS_TO_TICKS(ETH_WD_PERIOD_MS))
        return;
    s_eth_wd.last_watchdog_run = now;

    if (!s_eth_wd.enabled_cfg)
        return;
    if (!s_eth_wd.link_up_event)
        return;

    esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (!eth)
        return;

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(eth, &ip) != ESP_OK)
        return;

    bool has_ip_now = (ip.ip.addr != 0);

    // 1) Link up but no IP for too long
    if (!has_ip_now)
    {
        // Check if we're still in the grace period after a restart (be patient with DHCP)
        bool in_grace_period = (s_eth_wd.last_restart_time != 0 &&
                                (now - s_eth_wd.last_restart_time) < pdMS_TO_TICKS(ETH_WD_RESTART_GRACE_MS));

        TickType_t timeout_ms = in_grace_period ? ETH_WD_RESTART_GRACE_MS : ETH_WD_NO_IP_TIMEOUT_MS;

        if ((now - s_eth_wd.last_state_change) > pdMS_TO_TICKS(timeout_ms))
        {
            if (in_grace_period)
            {
                ESP_LOGW(TAG, "ETH-WD: link up but no IP for %u ms (still in grace period; threshold=%u ms)",
                         pdTICKS_TO_MS(now - s_eth_wd.last_state_change), timeout_ms);
            }
            eth_watchdog_force_restart("link up but no IP");
        }
        return;
    }

    // 2) If we have IP, but no successful activity for a while -> restart
    // (Skip this check during restart grace period to allow services to settle)
    bool in_grace_period = (s_eth_wd.last_restart_time != 0 &&
                            (now - s_eth_wd.last_restart_time) < pdMS_TO_TICKS(ETH_WD_RESTART_GRACE_MS));

    if (!in_grace_period)
    {
        if (s_eth_wd.last_successful_activity != 0)
        {
            if ((now - s_eth_wd.last_successful_activity) > pdMS_TO_TICKS(ETH_WD_NO_ACTIVITY_TIMEOUT_MS))
            {
                ESP_LOGW(TAG, "ETH-WD: IP present but no successful activity for %u ms; restarting", ETH_WD_NO_ACTIVITY_TIMEOUT_MS);
                eth_watchdog_force_restart("ip present but no activity");
                return;
            }
        }
        else
        {
            // No recorded activity yet; allow a short grace window after IP assignment
            if ((now - s_eth_wd.last_state_change) > pdMS_TO_TICKS(ETH_WD_NO_ACTIVITY_TIMEOUT_MS))
            {
                ESP_LOGW(TAG, "ETH-WD: IP present but never saw activity; restarting");
                eth_watchdog_force_restart("ip present but never active");
                return;
            }
        }
    }

    // 3) Gateway unreachable repeated failures (existing logic)
    if (!eth_watchdog_gateway_reachable())
    {
        s_eth_wd.gateway_fail_count++;
        LOGW(TAG, "ETH-WD: gateway unreachable (%d/%d)",
             s_eth_wd.gateway_fail_count, ETH_WD_GATEWAY_FAILS_MAX);
        if (s_eth_wd.gateway_fail_count >= ETH_WD_GATEWAY_FAILS_MAX)
        {
            eth_watchdog_force_restart("gateway unreachable");
            return;
        }
    }
    else
    {
        s_eth_wd.gateway_fail_count = 0;
    }

    // 4) Only apply active-netif logic when Ethernet is actually the current active interface.
    esp_netif_t *active = get_active_netif();
    if (active != eth)
    {
        const char *active_if = "none";
        if (active == esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"))
            active_if = "WIFI_STA_DEF";
        else if (active == esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"))
            active_if = "WIFI_AP_DEF";

        LOGD(TAG, "ETH-WD: Ethernet is not active netif (active=%s); skipping restart checks", active_if);
        return;
    }

    // If Ethernet is the active interface, update the last state change marker.
    s_eth_wd.last_state_change = now;

#if defined(ETH_CMD_G_PHY_LINK)
    // Optional PHY ioctl sanity check
    esp_eth_handle_t h = eth_get_handle();
    if (h)
    {
        int link = 0;
        if (esp_eth_ioctl(h, ETH_CMD_G_PHY_LINK, &link) == ESP_OK && link == 0)
        {
            LOGW(TAG, "ETH-WD: ioctl reports PHY link down despite events; forcing restart");
            eth_watchdog_force_restart("phy ioctl link down");
            return;
        }
    }
#endif
}

void network_manager_notify_config_change(void)
{
    if (network_manager_task_handle != NULL)
    {
        xTaskNotifyGive(network_manager_task_handle);
    }
}

void network_manager_request_mqtt_restart(void)
{
    s_request_mqtt_restart = true;

    if (network_manager_task_handle != NULL)
    {
        xTaskNotifyGive(network_manager_task_handle);
    }
}

void network_manager_request_actuals_update(void)
{
    if (network_manager_task_handle != NULL)
    {
        xTaskNotifyGive(network_manager_task_handle);
    }
}

bool network_manager_is_running(void)
{
    return manager_running;
}

// Event handlers

// Wi‑Fi events (STA + AP)
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (eth_is_shutting_down())
        return;

    if (base != WIFI_EVENT)
        return;

    bool eth_up = false;
    esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (eth_netif)
    {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(eth_netif, &ip) == ESP_OK && ip.ip.addr != 0)
            eth_up = true;
    }

    eth_up = eth_is_connected();

    switch (id)
    {
    case WIFI_EVENT_STA_START:
        if (eth_up)
        {
            LOGW(TAG, "Ethernet is up → suppressing WiFi STA start");
            return;
        }

        LOGI(TAG, "WIFI_EVENT_STA_START");

        if (eth_up)
        {
            LOGW(TAG, "Ethernet is up → suppressing WiFi STA start");
            return;
        }

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.wifi_connected = false;
            sm_actuals.wifi_error = false;
            sm_actuals.wifi_ap_mode = false;
            xSemaphoreGive(statemachine_mutex);
        }

        wifi_led_set_mode(LED_SLOW_BLINK);
        if (task_rgb_leds)
            xTaskNotifyGive(task_rgb_leds);

        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_CONNECTED:
        LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
        wifi_led_set_mode(LED_FAST_BLINK);
        break;

    case WIFI_EVENT_STA_DISCONNECTED:

        if (eth_up)
        {
            LOGW(TAG, "Ethernet is up → suppressing WiFi retry");
            return;
        }

        LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED");

        if (eth_up)
        {
            LOGW(TAG, "Ethernet is up → suppressing WiFi retry");
            return;
        }

        extern int s_retry_num;
        if (s_retry_num < MAXIMUM_RETRY)
        {
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                sm_actuals.wifi_connected = false;
                sm_actuals.wifi_error = true;
                xSemaphoreGive(statemachine_mutex);
            }

            wifi_led_set_mode(LED_SLOW_BLINK);
            if (task_rgb_leds)
                xTaskNotifyGive(task_rgb_leds);

            esp_wifi_connect();
            s_retry_num++;
            LOGW(TAG, "Retry to connect to the AP (%d/%d)", s_retry_num, MAXIMUM_RETRY);
        }
        else
        {
            if (s_wifi_event_group)
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            LOGE(TAG, "Failed to connect to AP after %d retries", MAXIMUM_RETRY);
        }

        s_request_syslog_update = true;
        if (network_manager_task_handle)
            xTaskNotifyGive(network_manager_task_handle);

        break;

    case WIFI_EVENT_AP_START:
        LOGI(TAG, "WIFI_EVENT_AP_START");

        if (!ethernet_config_is_enabled())
        {
            esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            if (ap)
                esp_netif_set_default_netif(ap);
        }

        wifi_led_set_mode(LED_SOLID);
        if (s_wifi_event_group)
            xEventGroupSetBits(s_wifi_event_group, AP_STARTED_BIT);

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.wifi_ap_mode = true;
            xSemaphoreGive(statemachine_mutex);
        }

        if (task_rgb_leds)
            xTaskNotifyGive(task_rgb_leds);

        s_request_syslog_update = true;
        if (network_manager_task_handle)
            xTaskNotifyGive(network_manager_task_handle);

        break;

    default:
        break;
    }
}

// IP events (Wi‑Fi + Ethernet)
static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (eth_is_shutting_down())
        return;

    if (base != IP_EVENT)
        return;

    switch (id)
    {
    case IP_EVENT_ETH_GOT_IP:
    {
        LOGI(TAG, "IP_EVENT_ETH_GOT_IP (handler)");

        // Update watchdog markers
        s_eth_wd.has_ip_event = true;
        s_eth_wd.last_state_change = xTaskGetTickCount();
        s_eth_wd.gateway_fail_count = 0;
        s_eth_wd.last_successful_activity = xTaskGetTickCount(); // treat IP assignment as activity

        // Update statemachine actuals: mark Ethernet up and force Wi‑Fi state cleared
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            // Ethernet is now primary, so mark it connected and link up; Wi‑Fi is now secondary, so mark it down
            sm_actuals.eth_connected = true;
            sm_actuals.eth_link_up = true;

            // Ensure Wi‑Fi is considered down while Ethernet is primary
            sm_actuals.wifi_connected = false;
            sm_actuals.wifi_error = false;
            sm_actuals.wifi_ap_mode = false;

            // Clear Wi‑Fi runtime info so sys_stats won't show stale values
            sm_actuals.wifi_rssi = -127;
            sm_actuals.wifi_channel = -1;
            sm_actuals.wifi_authmode = -1;
            sm_actuals.wifi_phy = 0;
            sm_actuals.wifi_bandwidth = 0;
            memset(sm_actuals.wifi_bssid, 0, sizeof(sm_actuals.wifi_bssid));
            memset(sm_actuals.wifi_ssid, 0, sizeof(sm_actuals.wifi_ssid));
            snprintf(sm_actuals.wifi_ssid, sizeof(sm_actuals.wifi_ssid), "%s", "00:00:00:00:00:00");
            snprintf(sm_actuals.wifi_ip_addr, sizeof(sm_actuals.wifi_ip_addr), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_netmask, sizeof(sm_actuals.wifi_netmask), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_gateway, sizeof(sm_actuals.wifi_gateway), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_dns1, sizeof(sm_actuals.wifi_dns1), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_dns2, sizeof(sm_actuals.wifi_dns2), "%s", "0.0.0.0");

            xSemaphoreGive(statemachine_mutex);
        }

        // Stop Wi‑Fi manager to ensure Wi‑Fi cannot remain active while Ethernet is primary.
        // wifi_manager_stop is safe to call here (it is idempotent).
        wifi_manager_stop();

        // Log Ethernet IP info for diagnostics
        {
            esp_netif_ip_info_t ip_info = {0};
            if (safe_eth_ip_info(&ip_info))
            {
                LOGI(TAG, "Network manager: Ethernet is up with IP " IPSTR, IP2STR(&ip_info.ip));
            }
            else
            {
                LOGI(TAG, "Network manager: Ethernet is up (ETH_DEF), but failed to read IP info");
            }
        }

        // Request manager-level actions and UI updates
        s_request_eth_set_default = true;
        s_request_update_actuals = true;
        s_request_syslog_update = true;
        s_request_mqtt_restart = true;

        // Update LED state to reflect Ethernet primary status
        update_network_led_state();

        // Wake network manager task to process deferred work
        if (network_manager_task_handle)
            xTaskNotifyGive(network_manager_task_handle);

        break;
    }

    case IP_EVENT_STA_GOT_IP:
    {
        LOGI(TAG, "IP_EVENT_STA_GOT_IP (handler)");
        if (!ethernet_config_is_enabled())
        {
            esp_netif_t *wifi = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (wifi)
                esp_netif_set_default_netif(wifi);
        }

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.wifi_connected = true;
            sm_actuals.wifi_error = false;
            xSemaphoreGive(statemachine_mutex);
        }

        {
            esp_netif_t *wifi_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip_info = {0};
            if (wifi_netif && esp_netif_get_ip_info(wifi_netif, &ip_info) == ESP_OK)
            {
                LOGI(TAG, "Network manager: Wi-Fi STA is up with IP %d.%d.%d.%d",
                     IP2STR(&ip_info.ip));
            }
            else
            {
                LOGI(TAG, "Network manager: Wi-Fi STA is up (WIFI_STA_DEF), but failed to read IP info");
            }
        }

        s_request_update_actuals = true;
        s_request_syslog_update = true;
        s_request_mqtt_restart = true;
        update_network_led_state();
        if (network_manager_task_handle)
            xTaskNotifyGive(network_manager_task_handle);
        break;
    }

    default:
        break;
    }
}

// Ethernet link events — FIXED VERSION (no static_ip_invalid)
static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (eth_is_shutting_down())
        return;

    esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (!eth)
    {
        LOGE(TAG, "ETHERNET_EVENT: ETH_DEF not found");
        return;
    }

    switch (id)
    {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "ETHERNET_EVENT_CONNECTED (handler)");

        s_eth_wd.link_up_event = true;
        s_eth_wd.has_ip_event = false;
        s_eth_wd.last_state_change = xTaskGetTickCount();
        s_eth_wd.gateway_fail_count = 0;

        // If Ethernet comes up, ensure Wi‑Fi is stopped and Wi‑Fi actuals cleared
        wifi_manager_stop();
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            sm_actuals.wifi_connected = false;
            sm_actuals.wifi_error = false;
            sm_actuals.wifi_ap_mode = false;

            sm_actuals.wifi_rssi = -127;
            sm_actuals.wifi_channel = -1;
            sm_actuals.wifi_authmode = -1;
            sm_actuals.wifi_phy = 0;
            sm_actuals.wifi_bandwidth = 0;
            memset(sm_actuals.wifi_bssid, 0, sizeof(sm_actuals.wifi_bssid));
            memset(sm_actuals.wifi_ssid, 0, sizeof(sm_actuals.wifi_ssid));

            snprintf(sm_actuals.wifi_ip_addr, sizeof(sm_actuals.wifi_ip_addr), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_netmask, sizeof(sm_actuals.wifi_netmask), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_gateway, sizeof(sm_actuals.wifi_gateway), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_dns1, sizeof(sm_actuals.wifi_dns1), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_dns2, sizeof(sm_actuals.wifi_dns2), "%s", "0.0.0.0");

            xSemaphoreGive(statemachine_mutex);
        }

        // Ask manager to refresh displays / syslog
        network_manager_request_actuals_update();
        update_syslog_state();

        if (statemachine_mutex &&
            xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.eth_link_up = true;
            sm_actuals.eth_connected = false;
            xSemaphoreGive(statemachine_mutex);
        }

        esp_netif_ip_info_t ip = {0};
        esp_err_t r = ESP_ERR_INVALID_STATE;
        if (eth_handle != NULL && eth_netif != NULL && !eth_is_shutting_down())
            r = esp_netif_get_ip_info(eth, &ip);

        bool static_ip_configured =
            (r == ESP_OK &&
             ip.ip.addr != 0 &&
             ip.netmask.addr != 0 &&
             ip.gw.addr != 0);

        if (!static_ip_configured)
        {
            ESP_LOGW(TAG, "Ethernet has no valid static IP; requesting DHCP start from manager");
            // Defer DHCP startup to the network manager task to avoid doing
            // heavy esp-netif operations in the event callback context.
            s_request_update_actuals = true;
            s_request_syslog_update = true;
        }
        else
        {
            s_request_update_actuals = true;
            s_request_syslog_update = true;
        }

        update_network_led_state();

        if (network_manager_task_handle)
            xTaskNotifyGive(network_manager_task_handle);

        break;

    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "ETHERNET_EVENT_DISCONNECTED (handler)");

        // Do not tear down the driver/netif here. HTTP and other TCP sockets may
        // still hold lwIP objects associated with this interface. The manager
        // keeps the resources alive so Ethernet can be restarted safely.

        s_eth_wd.link_up_event = false;
        s_eth_wd.has_ip_event = false;
        s_eth_wd.gateway_fail_count = 0;
        s_eth_wd.last_successful_activity = 0; // clear activity
        s_eth_wd.last_state_change = xTaskGetTickCount();

        if (statemachine_mutex &&
            xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.eth_link_up = false;
            sm_actuals.eth_connected = false;
            xSemaphoreGive(statemachine_mutex);
        }

        s_request_update_actuals = true;
        s_request_syslog_update = true;
        s_request_mqtt_restart = true;

        if (network_manager_task_handle)
            xTaskNotifyGive(network_manager_task_handle);

        break;

    default:
        break;
    }
}

// MQTT events
static void on_mqtt_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:
    {
        ESP_LOGD(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_set_connected(true);

        // Defer HA discovery to MQTT task
        mqtt_request_homeassistant_discovery();

        update_network_led_state();
        net_mgr_notify_eth_activity();

        break;
    }

    case MQTT_EVENT_DISCONNECTED:

        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");

        mqtt_set_connected(false);

        if (network_has_ip())
        {
            esp_netif_t *active = get_active_netif();
            esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");

            if (active == eth_netif)
            {
                eth_led_set_mode(LED_DOUBLE_BLINK);
            }
            else
            {
                wifi_led_set_mode(LED_DOUBLE_BLINK);
            }
        }
        break;

    case MQTT_EVENT_ERROR:
    {
        esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

        if (!event || !event->error_handle)
        {
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR but event_data is NULL");
            break;
        }

        ESP_LOGE(TAG, "MQTT_EVENT_ERROR: esp_err=%s tls_err=%d",
             esp_err_to_name(event->error_handle->esp_tls_last_esp_err),
             event->error_handle->esp_tls_stack_err);
        break;
    }

    default:
        break;
    }
}

static void register_event_handlers(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, &wifi_evt_inst));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL, &ip_evt_inst));

    if (hw.has_ethernet)
    {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, NULL, &eth_evt_inst));
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        MQTT_EVENTS, ESP_EVENT_ANY_ID, &on_mqtt_event, NULL, &mqtt_evt_inst));
}

// Register event handlers for Wi‑Fi, Ethernet, IP, and MQTT
static void unregister_event_handlers(void)
{
    if (wifi_evt_inst)
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt_inst);
    if (ip_evt_inst)
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_evt_inst);
    if (hw.has_ethernet && eth_evt_inst)
        esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_evt_inst);
    if (mqtt_evt_inst)
        esp_event_handler_instance_unregister(MQTT_EVENTS, ESP_EVENT_ANY_ID, mqtt_evt_inst);

    wifi_evt_inst = ip_evt_inst = eth_evt_inst = mqtt_evt_inst = NULL;
}

// Config evaluation and application
// Paste this entire function into network_manager.c (replace existing evaluate_config_and_apply)
static void evaluate_config_and_apply(void)
{
    // ------------------------------------------------------------------------
    // 1. Read current config snapshot
    // ------------------------------------------------------------------------
    bool wifi_enabled = false;
    bool eth_enabled = false;
    bool wifi_dhcp = true;
    bool eth_dhcp = true;
    bool wifi_static_valid = false;
    char hostname[64] = {0};
    char wifi_ip_address[MEDIUM_CONFIG_ITEM] = {0};
    char wifi_subnet_mask[MEDIUM_CONFIG_ITEM] = {0};
    char wifi_gateway[MEDIUM_CONFIG_ITEM] = {0};
    char wifi_primary_dns[MEDIUM_CONFIG_ITEM] = {0};
    char eth_ip_address[MEDIUM_CONFIG_ITEM] = {0};
    char eth_subnet_mask[MEDIUM_CONFIG_ITEM] = {0};
    char eth_gateway[MEDIUM_CONFIG_ITEM] = {0};
    char eth_primary_dns[MEDIUM_CONFIG_ITEM] = {0};

    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        wifi_enabled = (strcmp(wifi_config.enable_wifi, "On") == 0);
        eth_enabled = (strcmp(ethernet_config.enable_ethernet, "On") == 0);
        wifi_dhcp = (strcmp(wifi_config.enable_dhcp, "On") == 0);
        eth_dhcp = (strcmp(ethernet_config.enable_dhcp, "On") == 0);
        strncpy(hostname, wifi_config.hostname, sizeof(hostname) - 1);
        strncpy(wifi_ip_address, wifi_config.ip_address, sizeof(wifi_ip_address) - 1);
        strncpy(wifi_subnet_mask, wifi_config.subnet_mask, sizeof(wifi_subnet_mask) - 1);
        strncpy(wifi_gateway, wifi_config.gateway, sizeof(wifi_gateway) - 1);
        strncpy(wifi_primary_dns, wifi_config.primary_dns, sizeof(wifi_primary_dns) - 1);
        strncpy(eth_ip_address, ethernet_config.ip_address, sizeof(eth_ip_address) - 1);
        strncpy(eth_subnet_mask, ethernet_config.subnet_mask, sizeof(eth_subnet_mask) - 1);
        strncpy(eth_gateway, ethernet_config.gateway, sizeof(eth_gateway) - 1);
        strncpy(eth_primary_dns, ethernet_config.primary_dns, sizeof(eth_primary_dns) - 1);
        xSemaphoreGive(network_config_mutex);
    }
    else
    {
        LOGW(TAG, "evaluate_config_and_apply: could not take network_config_mutex");
        return;
    }

    wifi_static_valid = is_valid_ip_address(wifi_ip_address) &&
                        is_valid_ip_address(wifi_subnet_mask) &&
                        is_valid_ip_address(wifi_gateway) &&
                        is_valid_ip_address(wifi_primary_dns);

    s_eth_wd.enabled_cfg = eth_enabled;

    // ------------------------------------------------------------------------
    // 2. STATIC snapshot to detect changes
    // ------------------------------------------------------------------------
    typedef struct
    {
        bool wifi_enabled;
        bool eth_enabled;
        bool wifi_dhcp;
        bool eth_dhcp;
        char hostname[64];
        char wifi_ip_address[MEDIUM_CONFIG_ITEM];
        char wifi_subnet_mask[MEDIUM_CONFIG_ITEM];
        char wifi_gateway[MEDIUM_CONFIG_ITEM];
        char wifi_primary_dns[MEDIUM_CONFIG_ITEM];
        char eth_ip_address[MEDIUM_CONFIG_ITEM];
        char eth_subnet_mask[MEDIUM_CONFIG_ITEM];
        char eth_gateway[MEDIUM_CONFIG_ITEM];
        char eth_primary_dns[MEDIUM_CONFIG_ITEM];
    } cfg_snapshot_t;

    static cfg_snapshot_t last_cfg = {0};
    cfg_snapshot_t current_cfg = {
        .wifi_enabled = wifi_enabled,
        .eth_enabled = eth_enabled,
        .wifi_dhcp = wifi_dhcp,
        .eth_dhcp = eth_dhcp};
    strncpy(current_cfg.hostname, hostname, sizeof(current_cfg.hostname) - 1);
    strncpy(current_cfg.wifi_ip_address, wifi_ip_address, sizeof(current_cfg.wifi_ip_address) - 1);
    strncpy(current_cfg.wifi_subnet_mask, wifi_subnet_mask, sizeof(current_cfg.wifi_subnet_mask) - 1);
    strncpy(current_cfg.wifi_gateway, wifi_gateway, sizeof(current_cfg.wifi_gateway) - 1);
    strncpy(current_cfg.wifi_primary_dns, wifi_primary_dns, sizeof(current_cfg.wifi_primary_dns) - 1);

    strncpy(current_cfg.eth_ip_address, eth_ip_address, sizeof(current_cfg.eth_ip_address) - 1);
    strncpy(current_cfg.eth_subnet_mask, eth_subnet_mask, sizeof(current_cfg.eth_subnet_mask) - 1);
    strncpy(current_cfg.eth_gateway, eth_gateway, sizeof(current_cfg.eth_gateway) - 1);
    strncpy(current_cfg.eth_primary_dns, eth_primary_dns, sizeof(current_cfg.eth_primary_dns) - 1);

    // ------------------------------------------------------------------------
    // 3. Evaluate Ethernet state (link + IP)
    // ------------------------------------------------------------------------
    bool eth_has_ip = false;
    bool eth_link_up = false;

    if (hw.has_ethernet)
    {
        if (eth_handle != NULL && !eth_is_shutting_down())
        {
            esp_netif_t *ethnet = esp_netif_get_handle_from_ifkey("ETH_DEF");
            if (ethnet)
            {
                esp_netif_ip_info_t ip;
                eth_link_up = ethernet_link_is_up();
                if (safe_eth_ip_info(&ip) && ip.ip.addr != 0)
                    eth_has_ip = true;
            }
        }
    }

    // If nothing changed and Ethernet is healthy, skip reapply
    if (memcmp(&current_cfg, &last_cfg, sizeof(cfg_snapshot_t)) == 0 && eth_enabled && eth_has_ip)
    {
        LOGD(TAG, "evaluate_config_and_apply: config unchanged + Ethernet OK → skip");
        return;
    }

    // Save snapshot (we are going to apply changes)
    last_cfg = current_cfg;

    // ------------------------------------------------------------------------
    // 4. Update statemachine actuals for enabled flags
    // ------------------------------------------------------------------------
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        sm_actuals.wifi_enabled = wifi_enabled;
        sm_actuals.ethernet_enabled = eth_enabled;
        sm_actuals.wifi_dhcp_enabled = wifi_dhcp;
        sm_actuals.eth_dhcp_enabled = eth_dhcp;
        xSemaphoreGive(statemachine_mutex);
    }

    // ------------------------------------------------------------------------
    // 5. Apply hostname to netifs
    // ------------------------------------------------------------------------
    if (hostname[0] != '\0')
    {
        esp_netif_t *n;
        n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (n)
            esp_netif_set_hostname(n, hostname);
        n = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (n)
            esp_netif_set_hostname(n, hostname);
    }

    // Bring up the fail-safe AP before stopping Ethernet so the default route
    // always has a valid replacement when both configured interfaces are off.
    if (!wifi_enabled && !eth_enabled)
    {
        if (wifi_manager_init() == ESP_OK && !wifi_manager_ap_is_running())
        {
            esp_err_t ap_r = wifi_manager_start_ap();
            if (ap_r != ESP_OK)
                LOGW(TAG, "evaluate_config_and_apply: fail-safe AP start failed: %s", esp_err_to_name(ap_r));
        }
    }

    // ------------------------------------------------------------------------
    // 6. Ethernet handling (init/stop/apply config)
    // ------------------------------------------------------------------------
    if (hw.has_ethernet)
    {
        if (eth_enabled)
        {
            eth_init();
            ethernet_config_apply();
            if (!sm_actuals.eth_connected)
                eth_led_set_mode(LED_SLOW_BLINK);
        }
        else
        {
            // Move the default route away from ETH_DEF before stopping the
            // driver; active sockets may still emit traffic during shutdown.
            esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
            if (eth && esp_netif_get_default_netif() == eth)
            {
                esp_netif_t *fallback = get_active_netif();
                if (fallback)
                    esp_netif_set_default_netif(fallback);
            }

            eth_stop();
            eth_led_set_mode(LED_OFF);
        }
    }

    // ------------------------------------------------------------------------
    // 7. Wi‑Fi handling and suppression logic
    //    - If Ethernet has IP: force Wi‑Fi stopped and clear Wi‑Fi actuals.
    //    - If Ethernet link up but DHCP pending: defer Wi‑Fi until DHCP settles.
    // ------------------------------------------------------------------------
    if (!wifi_enabled)
    {
        if (!eth_enabled)
        {
            LOGW(TAG, "evaluate_config_and_apply: Wi-Fi and Ethernet are disabled; keeping the configuration AP available");
            if (wifi_manager_init() != ESP_OK)
            {
                LOGW(TAG, "evaluate_config_and_apply: wifi_manager_init failed while starting configuration AP");
                return;
            }

            if (!wifi_manager_ap_is_running())
            {
                esp_err_t ap_r = wifi_manager_start_ap();
                if (ap_r != ESP_OK)
                    LOGW(TAG, "evaluate_config_and_apply: configuration AP start failed: %s", esp_err_to_name(ap_r));
            }
            return;
        }

        // Wi‑Fi disabled in config: ensure it's stopped and clear Wi‑Fi actuals
        wifi_manager_stop();
        wifi_led_set_mode(LED_OFF);

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            // Clear Wi‑Fi fields to avoid stale display
            sm_actuals.wifi_enabled = false;
            sm_actuals.wifi_connected = false;
            sm_actuals.wifi_error = false;
            sm_actuals.wifi_ap_mode = false;

            sm_actuals.wifi_rssi = -127;
            sm_actuals.wifi_channel = -1;
            sm_actuals.wifi_authmode = -1;
            sm_actuals.wifi_phy = 0;
            sm_actuals.wifi_bandwidth = 0;
            memset(sm_actuals.wifi_bssid, 0, sizeof(sm_actuals.wifi_bssid));
            memset(sm_actuals.wifi_ssid, 0, sizeof(sm_actuals.wifi_ssid));

            snprintf(sm_actuals.wifi_ip_addr, sizeof(sm_actuals.wifi_ip_addr), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_netmask, sizeof(sm_actuals.wifi_netmask), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_gateway, sizeof(sm_actuals.wifi_gateway), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_dns1, sizeof(sm_actuals.wifi_dns1), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_dns2, sizeof(sm_actuals.wifi_dns2), "%s", "0.0.0.0");

            xSemaphoreGive(statemachine_mutex);
        }

        return;
    }

    // Ensure Wi‑Fi manager initialized
    if (wifi_manager_init() != ESP_OK)
    {
        LOGW(TAG, "evaluate_config_and_apply: wifi_manager_init failed");
        return;
    }

    // Ensure DHCP client state for Wi‑Fi netif matches config
    esp_netif_t *wifinet = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (wifinet)
    {
        if (wifi_dhcp)
            esp_netif_dhcpc_start(wifinet);
        else
            esp_netif_dhcpc_stop(wifinet);
    }

    // If Ethernet has a valid IP, enforce Wi‑Fi stopped and clear Wi‑Fi actuals
    if (eth_enabled && eth_has_ip)
    {
        LOGI(TAG, "evaluate_config_and_apply: Ethernet has IP → enforce Wi‑Fi suppressed (Ethernet primary)");

        esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (eth_netif)
        {
            esp_netif_set_default_netif(eth_netif);
            LOGI(TAG, "evaluate_config_and_apply: Ethernet set as default netif");
        }

        // Stop Wi‑Fi and clear Wi‑Fi actuals
        wifi_manager_stop();
        wifi_led_set_mode(LED_OFF);

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            sm_actuals.wifi_connected = false;
            sm_actuals.wifi_error = false;
            sm_actuals.wifi_ap_mode = false;

            sm_actuals.wifi_rssi = -127;
            sm_actuals.wifi_channel = -1;
            sm_actuals.wifi_authmode = -1;
            sm_actuals.wifi_phy = 0;
            sm_actuals.wifi_bandwidth = 0;
            memset(sm_actuals.wifi_bssid, 0, sizeof(sm_actuals.wifi_bssid));
            memset(sm_actuals.wifi_ssid, 0, sizeof(sm_actuals.wifi_ssid));

            snprintf(sm_actuals.wifi_ip_addr, sizeof(sm_actuals.wifi_ip_addr), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_netmask, sizeof(sm_actuals.wifi_netmask), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_gateway, sizeof(sm_actuals.wifi_gateway), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_dns1, sizeof(sm_actuals.wifi_dns1), "%s", "0.0.0.0");
            snprintf(sm_actuals.wifi_dns2, sizeof(sm_actuals.wifi_dns2), "%s", "0.0.0.0");

            xSemaphoreGive(statemachine_mutex);
        }

        return;
    }

    // If Ethernet link is up but DHCP pending, defer Wi‑Fi until DHCP settles
    if (eth_enabled && eth_link_up)
    {
        LOGI(TAG, "evaluate_config_and_apply: Ethernet link is up and DHCP pending; deferring Wi‑Fi STA/AP");
        wifi_manager_stop();
        wifi_led_set_mode(LED_OFF);
        return;
    }

    // If Ethernet is enabled but not yet established, do not start Wi‑Fi yet.
    // This prevents the startup race where Wi‑Fi gets an IP before Ethernet finishes
    // bringing up, then is later disabled when Ethernet becomes primary.
    if (eth_enabled && hw.has_ethernet && !eth_has_ip && eth_handle != NULL && !eth_is_shutting_down())
    {
        TickType_t now = xTaskGetTickCount();
        TickType_t start_tick = s_eth_wd.last_state_change;
        if (start_tick == 0)
        {
            start_tick = now;
            s_eth_wd.last_state_change = now;
        }

        uint32_t failover_timeout_ms = eth_link_up
                                           ? ETH_WD_NO_IP_TIMEOUT_MS
                                           : ETH_WD_NO_LINK_TIMEOUT_MS;
        if ((now - start_tick) < pdMS_TO_TICKS(failover_timeout_ms))
        {
            LOGI(TAG, "evaluate_config_and_apply: Ethernet %s; deferring Wi-Fi startup for %u ms",
                 eth_link_up ? "link up with no IP" : "link not detected",
                 (unsigned)failover_timeout_ms);
            wifi_manager_stop();
            wifi_led_set_mode(LED_OFF);
            return;
        }
    }

    // Otherwise, Ethernet not present/usable — proceed to Wi‑Fi STA connect if credentials exist
    bool have_credentials = false;
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        have_credentials = (strlen(wifi_config.ssid) > 0 && strlen(wifi_config.wifi_password) > 0);
        xSemaphoreGive(network_config_mutex);
    }

    if (!wifi_dhcp && !wifi_static_valid)
    {
        LOGW(TAG, "evaluate_config_and_apply: Wi-Fi DHCP is off but static IP data is invalid; starting AP");
        if (!wifi_manager_ap_is_running())
        {
            wifi_manager_stop();
            wifi_led_set_mode(LED_OFF);
            esp_err_t ap_r = wifi_manager_start_ap();
            if (ap_r != ESP_OK)
                LOGW(TAG, "evaluate_config_and_apply: AP start failed: %s", esp_err_to_name(ap_r));
        }
    }
    else if (have_credentials)
    {
        LOGI(TAG, "evaluate_config_and_apply: attempting Wi‑Fi STA connect");
        esp_err_t r = wifi_manager_connect(&wifi_config);
        if (r == ESP_OK)
        {
            LOGI(TAG, "evaluate_config_and_apply: Wi‑Fi STA connect succeeded");
        }
        else
        {
            LOGW(TAG, "evaluate_config_and_apply: Wi-Fi STA connect returned %s; starting AP", esp_err_to_name(r));
            if (!wifi_manager_ap_is_running())
            {
                wifi_manager_stop();
                wifi_led_set_mode(LED_OFF);
                esp_err_t ap_r = wifi_manager_start_ap();
                if (ap_r != ESP_OK)
                    LOGW(TAG, "evaluate_config_and_apply: AP start failed: %s", esp_err_to_name(ap_r));
            }
        }
    }
    else
    {
        LOGI(TAG, "evaluate_config_and_apply: no Wi-Fi credentials; starting AP");
        if (!wifi_manager_ap_is_running())
        {
            wifi_manager_stop();
            wifi_led_set_mode(LED_OFF);
            esp_err_t ap_r = wifi_manager_start_ap();
            if (ap_r != ESP_OK)
                LOGW(TAG, "evaluate_config_and_apply: AP start failed: %s", esp_err_to_name(ap_r));
        }
    }
}

// Complete network_manager_task implementation for network_manager.c
// Replace the existing function with this version. It centralizes all
// heavy network operations in the manager task, keeps event handlers tiny,
// and processes deferred requests/teardown safely.
static void network_manager_task(void *pvParameters)
{
    (void)pvParameters;
    LOGI(TAG, "Network manager starting");

    // Initialize LED GPIOs and TCP/IP stack (idempotent)
    eth_led_init();

    // Ensure esp-netif is initialized (safe to call multiple times)
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop if not already present
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
    }

    // Register event handlers (Wi‑Fi, IP, ETH, MQTT)
    register_event_handlers();

    manager_running = true;
    LOGI(TAG, "network_manager_task: manager_running = true");

    // Initial evaluation and publish of actuals
    evaluate_config_and_apply();
    sys_stats_update_actuals();

    // DHCP recovery/backoff state
    static int dhcp_restart_backoff = 0;
    const int dhcp_backoff_max = 6;    // cap backoff exponent
    const int dhcp_backoff_base_s = 5; // base delay seconds for backoff
    static TickType_t last_status_log = 0;
    const TickType_t status_log_interval = pdMS_TO_TICKS(60000); // log every 60s

    // If ETH netif exists at startup and has no IP, start DHCP (idempotent)
    if (hw.has_ethernet)
    {
        esp_netif_t *eth_start = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (eth_start)
        {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(eth_start, &ip) == ESP_OK && ip.ip.addr == 0)
            {
                ESP_LOGW(TAG, "startup: ETH has no IP — starting DHCP client");
                esp_err_t r = esp_netif_dhcpc_start(eth_start);
                if (r != ESP_OK && r != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
                    ESP_LOGW(TAG, "esp_netif_dhcpc_start (eth) returned %s", esp_err_to_name(r));
                s_request_update_actuals = true;
                if (network_manager_task_handle)
                    xTaskNotifyGive(network_manager_task_handle);
            }
        }
        else
        {
            ESP_LOGW(TAG, "startup: ETH_DEF netif not found");
        }
    }

    // Main loop: wait for notifications or periodic wakeups and process requests
    for (;;)
    {
        const TickType_t wait_ticks = pdMS_TO_TICKS(30000);

        // Clear notification count and wait
        ulTaskNotifyTake(pdTRUE, wait_ticks);

        // Ethernet watchdog (non-blocking)
        eth_watchdog_tick();

        // DHCP health check and recovery. If link is up but IP is zero, attempt to start DHCP (with backoff).
        if (hw.has_ethernet)
        {
            esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
            if (eth && eth_handle != NULL && !eth_is_shutting_down())
            {
                esp_netif_ip_info_t ip;
                esp_err_t r = esp_netif_get_ip_info(eth, &ip);
                bool link = eth_is_connected();
                bool eth_dhcp_enabled = ethernet_config_is_dhcp_enabled();

                if (r == ESP_OK)
                {
                    if (link && ip.ip.addr == 0 && eth_dhcp_enabled)
                    {
                        ESP_LOGW(TAG, "network_manager_task: ETH link up but no IP — attempting DHCP (backoff=%d)", dhcp_restart_backoff);
                        esp_err_t s = esp_netif_dhcpc_start(eth);
                        if (s == ESP_OK || s == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
                        {
                            ESP_LOGI(TAG, "network_manager_task: dhcpc_start called");
                            dhcp_restart_backoff = 0; // reset on success
                        }
                        else
                        {
                            ESP_LOGW(TAG, "network_manager_task: esp_netif_dhcpc_start returned %s", esp_err_to_name(s));
                            if (dhcp_restart_backoff < dhcp_backoff_max)
                                dhcp_restart_backoff++;
                            int delay_s = dhcp_backoff_base_s << dhcp_restart_backoff;
                            // Small delay to avoid tight loop; allow other tasks to run
                            vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
                        }

                        // Request actuals/syslog update so UI reflects current state
                        s_request_update_actuals = true;
                        s_request_syslog_update = true;
                    }
                    else if (ip.ip.addr != 0)
                    {
                        // IP present — reset backoff in case we had previous issues
                        dhcp_restart_backoff = 0;
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "network_manager_task: esp_netif_get_ip_info(ETH_DEF) failed: %s", esp_err_to_name(r));
                }
            }
            else
            {
                ESP_LOGW(TAG, "network_manager_task: ETH_DEF netif not found");
            }
        }

        // Handle any deferred teardown that was previously deferred
        handle_deferred_eth_teardown_if_needed();

        // Periodic network status log at regular intervals
        {
            TickType_t now = xTaskGetTickCount();
            if (last_status_log == 0 || (now - last_status_log) >= status_log_interval)
            {
                log_network_status();
                last_status_log = now;
            }
        }

        // If requested, set ETH as default netif (do this from manager task)
        if (s_request_eth_set_default)
        {
            s_request_eth_set_default = false;
            esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
            if (eth)
            {
                esp_netif_set_default_netif(eth);
                LOGI(TAG, "network_manager_task: ETH set as default route (manager)");
            }
            else
            {
                LOGW(TAG, "network_manager_task: requested to set ETH default but ETH_DEF not found");
            }

            // After forcing ETH default, ensure services restart to bind to the new route
            s_request_update_actuals = true;
            s_request_syslog_update = true;
            s_request_mqtt_restart = true;
        }

        // Also ensure the default route follows Ethernet whenever Ethernet is the active interface.
        if (hw.has_ethernet && eth_is_connected())
        {
            esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
            if (eth && esp_netif_get_default_netif() != eth)
            {
                esp_netif_set_default_netif(eth);
                LOGI(TAG, "network_manager_task: ETH default route restored because Ethernet is active");
            }
        }

        // Re-evaluate configuration and apply changes (safe, nonblocking)
        evaluate_config_and_apply();

        // If requested, refresh actuals by reading netif state and DNS info
        if (s_request_update_actuals)
        {
            s_request_update_actuals = false;

            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                // Reset fields we will update
                sm_actuals.wifi_connected = false;
                sm_actuals.eth_connected = false;
                sm_actuals.wifi_ap_mode = false;
                sm_actuals.hostname[0] = '\0';

                // Wi‑Fi STA
                esp_netif_t *wifinet = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (wifinet)
                {
                    esp_netif_ip_info_t ip;
                    if (esp_netif_get_ip_info(wifinet, &ip) == ESP_OK && ip.ip.addr != 0)
                    {
                        sm_actuals.wifi_connected = true;
                    }
                    const char *hn = NULL;
                    if (esp_netif_get_hostname(wifinet, &hn) == ESP_OK && hn)
                    {
                        snprintf(sm_actuals.hostname, sizeof(sm_actuals.hostname), "%s", hn);
                    }
                }

                // Ethernet
                esp_netif_t *ethnet = esp_netif_get_handle_from_ifkey("ETH_DEF");
                if (ethnet)
                {
                    esp_netif_ip_info_t ip;
                    if (esp_netif_get_ip_info(ethnet, &ip) == ESP_OK && ip.ip.addr != 0)
                    {
                        sm_actuals.eth_connected = true;
                        snprintf(sm_actuals.eth_ip_addr, sizeof(sm_actuals.eth_ip_addr), IPSTR, IP2STR(&ip.ip));
                        snprintf(sm_actuals.eth_netmask, sizeof(sm_actuals.eth_netmask), IPSTR, IP2STR(&ip.netmask));
                        snprintf(sm_actuals.eth_gateway, sizeof(sm_actuals.eth_gateway), IPSTR, IP2STR(&ip.gw));

                        esp_netif_dns_info_t dns;
                        if (esp_netif_get_dns_info(ethnet, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK)
                            snprintf(sm_actuals.eth_dns1, sizeof(sm_actuals.eth_dns1), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
                        if (esp_netif_get_dns_info(ethnet, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK)
                            snprintf(sm_actuals.eth_dns2, sizeof(sm_actuals.eth_dns2), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
                    }

                    // If hostname not set by Wi‑Fi, try Ethernet hostname (they should be the same, but just in case)
                    if (sm_actuals.hostname[0] == '\0')
                    {
                        const char *hn = NULL;
                        if (esp_netif_get_hostname(ethnet, &hn) == ESP_OK && hn)
                            snprintf(sm_actuals.hostname, sizeof(sm_actuals.hostname), "%s", hn);
                    }
                }

                xSemaphoreGive(statemachine_mutex);
            }
            else
            {
                LOGW(TAG, "network_manager_task: could not take statemachine_mutex to update actuals");
            }

            // Publish actuals and update syslog state
            sys_stats_update_actuals();

            // Ensure syslog state is updated immediately after actuals change so UI reflects current network state
            s_request_syslog_update = true;
        }

        // Handle deferred MQTT restart request (set by event handlers or apply_active_netif_changes)
        if (s_request_mqtt_restart)
        {
            s_request_mqtt_restart = false;

            MQTT_config_t cfg;
            mqtt_get_config_snapshot(&cfg);

            mqtt_stop();

            if (!mqtt_is_enabled_in_config())
            {
                LOGI(TAG, "network_manager_task: MQTT disabled; client remains stopped");
            }
            else
            {
                LOGI(TAG, "network_manager_task: restarting MQTT client");
                vTaskDelay(pdMS_TO_TICKS(300));
                if (mqtt_start(&cfg) != ESP_OK)
                {
                    LOGW(TAG, "network_manager_task: mqtt_start failed");
                }
                else
                {
                    LOGI(TAG, "network_manager_task: mqtt restarted");
                }
            }
        }

        // Process deferred syslog update
        if (s_request_syslog_update)
        {
            s_request_syslog_update = false;
            LOGI(TAG, "network_manager_task: processing deferred syslog update");
            update_syslog_state();
        }

        // Loop continues; do not unregister handlers or delete the task here
    }

    // Cleanup path (only reached if the loop is exited intentionally)
    unregister_event_handlers();
    manager_running = false;
    LOGI(TAG, "network_manager_task: exiting");
    vTaskDelete(NULL);
}

bool network_manager_start(void)
{
    if (network_manager_task_handle != NULL)
    {
        LOGI(TAG, "network_manager_start: already running (task=%p)", (void *)network_manager_task_handle);
        return true;
    }

    network_manager_task_handle = xTaskCreateStatic(network_manager_task, "net_mgr", network_manager_stack_size, NULL, 9, network_manager_stack, &network_manager_tcb);

    if (network_manager_task_handle == NULL)
    {
        LOGE(TAG, "network_manager_start: failed to create task");
        return false;
    }

    manager_running = true;
    LOGI(TAG, "network_manager_start: started (task=%p)", (void *)network_manager_task_handle);
    return true;
}

// Call this from other modules when a real network exchange succeeded on Ethernet
void net_mgr_notify_eth_activity(void)
{
    // Only update when Ethernet is enabled in config
    if (!s_eth_wd.enabled_cfg)
        return;

    esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (!eth)
        return;

    esp_netif_t *active = get_active_netif();
    if (active != eth)
        return;

    // Update timestamp only when Ethernet is the selected active interface
    s_eth_wd.last_successful_activity = xTaskGetTickCount();
}
