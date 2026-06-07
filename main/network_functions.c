/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "network_functions.h"

static const char *TAG = "network_func";

static TaskHandle_t eth_led_task = NULL;
static TaskHandle_t wifi_led_task = NULL;

static led_mode_t eth_led_mode = LED_OFF;
static led_mode_t wifi_led_mode = LED_OFF;

// Helper function to enable or disable syslog based on current network state
void update_syslog_state(void)
{
    static bool syslog_running = false;

    bool ready = network_has_ip(); // Only true after DHCP/static IP is applied

    // Read config snapshot under mutex to log what the config says
    char cfg_enable[11] = {0};
    if (syslog_config_mutex && xSemaphoreTake(syslog_config_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        strncpy(cfg_enable, syslog_config.enable_syslog, sizeof(cfg_enable) - 1);
        xSemaphoreGive(syslog_config_mutex);
    }
    else
    {
        strncpy(cfg_enable, "<unknown>", sizeof(cfg_enable) - 1);
    }

    LOGD("syslog", "update_syslog_state: network_ready=%d config_enable=%s syslog_running=%d",
         ready, cfg_enable, syslog_running);

    if (ready && !syslog_running)
    {
        syslog_enable();
        syslog_running = true;
        LOGI("syslog", "Syslog enabled");
    }
    else if (!ready && syslog_running)
    {
        syslog_disable();
        syslog_running = false;
        LOGI("syslog", "Syslog disabled");
    }
}

bool is_valid_ip_address(const char *ip_address)
{
    if (!ip_address || strlen(ip_address) == 0)
    {
        return false;
    }
    struct in_addr addr;
    return inet_pton(AF_INET, ip_address, &addr) == 1;
}

// Function to check if Wi-Fi has an IP
bool wifi_has_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL)
    {
        LOGW(TAG, "Wi-Fi STA netif handle for WIFI_STA_DEF not found");
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        return false;
    }
    return ip_info.ip.addr != 0;
}

// Prefer Ethernet, then WiFi STA, based on actual IP presence.
esp_netif_t *get_active_netif(void)
{
    /* Prefer Ethernet only when the PHY/link is actually up and the ETH netif has a valid IP.
       Fall back to Wi‑Fi STA only when it has a valid IP. */
    if (hw.has_ethernet)
    {
        esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (eth)
        {
            /* ethernet_link_is_up() checks PHY link (ioctl fallback to IP presence).
               This avoids treating an attached-but-unlinked netif as "active". */
            if (ethernet_link_is_up())
            {
                esp_netif_ip_info_t ip;
                if (esp_netif_get_ip_info(eth, &ip) == ESP_OK && ip.ip.addr != 0)
                    return eth;
            }
        }
    }

    /* Wi‑Fi STA: return if it has a valid IP */
    esp_netif_t *wifi = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (wifi)
    {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(wifi, &ip) == ESP_OK && ip.ip.addr != 0)
            return wifi;
    }

    return NULL;
}

bool network_has_ip(void)
{
    esp_netif_t *netif = get_active_netif();
    return (netif != NULL);
}

void update_network_led_state(void)
{
    esp_netif_t *active = get_active_netif();
    esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    esp_netif_t *wifi = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (active == eth)
    {
        eth_led_set_mode(LED_SOLID);
        wifi_led_set_mode(LED_OFF);
    }
    else if (active == wifi)
    {
        wifi_led_set_mode(LED_SOLID);
        eth_led_set_mode(LED_OFF);
    }
}

void update_hostname(void)
{
    const char *hostname = NULL;
    esp_netif_t *netif = NULL;

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        if (hw.has_ethernet && sm_actuals.eth_connected)
            netif = esp_netif_get_handle_from_ifkey("ETH_DEF");

        else if (sm_actuals.wifi_connected)
            netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

        if (netif && esp_netif_get_hostname(netif, &hostname) == ESP_OK && hostname)
            snprintf(sm_actuals.hostname, sizeof(sm_actuals.hostname), "%s", hostname);
        else
            snprintf(sm_actuals.hostname, sizeof(sm_actuals.hostname), "%s", "<unknown>");

        xSemaphoreGive(statemachine_mutex);
    }
}

// Ping to gateway functions
void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted = 0, received = 0, duration = 0, avg_time = 0;

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &duration, sizeof(duration));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &avg_time, sizeof(avg_time));

    int loss_percent = 0;
    if (transmitted > 0)
    {
        loss_percent = (int)((transmitted - received) * 100 / transmitted);
    }

    g_ping_summary.transmitted = (int)transmitted;
    g_ping_summary.received = (int)received;
    g_ping_summary.loss_percent = loss_percent;
    g_ping_summary.avg_time_ms = (int)avg_time;
    g_ping_summary.duration_ms = (int)duration;
    g_ping_summary.success = (received > 0);

    if (g_ping_summary.success)
    {
        net_mgr_notify_eth_activity();
    }

    LOGD(TAG, "Ping finished: tx=%d rx=%d loss=%d%% avg_rtt=%d ms duration=%d ms",
         g_ping_summary.transmitted, g_ping_summary.received,
         g_ping_summary.loss_percent, g_ping_summary.avg_time_ms,
         g_ping_summary.duration_ms);

    // Clean up session
    esp_ping_delete_session(hdl);

    // Signal completion
    if (g_ping_done_sem)
    {
        xSemaphoreGive(g_ping_done_sem);
    }
}

bool ping_gateway(void)
{
    esp_netif_t *netif = get_active_netif();
    if (!netif)
    {
        // Optional: one generic log, not “Wi‑Fi STA not found”
        LOGW(TAG, "No active network interface");
        return false; // or keep waiting
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        LOGE(TAG, "Failed to get IP info");
        return false;
    }

    // Log gateway using IPSTR/IP2STR to avoid incompatible-pointer warnings
    LOGD(TAG, "Pinging gateway: " IPSTR, IP2STR(&ip_info.gw));

    // Build ip_addr_t target_addr from ip_info.gw
    ip_addr_t target_addr;
    IP_ADDR4(&target_addr, ip4_addr1(&ip_info.gw), ip4_addr2(&ip_info.gw), ip4_addr3(&ip_info.gw), ip4_addr4(&ip_info.gw));

    LOGD(TAG, "Pinging gateway: " IPSTR, IP2STR(&ip_info.gw));

    // Reset summary
    memset(&g_ping_summary, 0, sizeof(g_ping_summary));

    // Create (or reuse) semaphore
    if (g_ping_done_sem == NULL)
    {
        g_ping_done_sem = xSemaphoreCreateBinary();
        if (g_ping_done_sem == NULL)
        {
            LOGE(TAG, "Failed to create semaphore");
            return false;
        }
    }
    else
    {
        // Drain any previous signal
        xSemaphoreTake(g_ping_done_sem, 0);
    }

    // Configure ping session
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = 5;                 // number of probes
    config.interval_ms = 1000;        // 1s between pings
    config.timeout_ms = 1000;         // per‑ping timeout
    config.target_addr = target_addr; // gateway

    esp_ping_callbacks_t cbs = {
        .on_ping_end = on_ping_end,
        // You can add per‑reply logging if you want:
        // .on_ping_success = your_success_cb,
        // .on_ping_timeout = your_timeout_cb,
    };

    // Start session
    esp_ping_handle_t ping = NULL;
    esp_err_t err = esp_ping_new_session(&config, &cbs, &ping);
    if (err != ESP_OK || ping == NULL)
    {
        LOGE(TAG, "esp_ping_new_session failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ping_start(ping);
    if (err != ESP_OK)
    {
        LOGE(TAG, "esp_ping_start failed: %s", esp_err_to_name(err));
        esp_ping_delete_session(ping);
        return false;
    }

    // Wait for completion (5 pings + buffer). Duration profile will be ~count * interval + timeouts; use a safe upper bound
    const TickType_t wait_ticks = pdMS_TO_TICKS(10000);
    if (xSemaphoreTake(g_ping_done_sem, wait_ticks) != pdTRUE)
    {
        LOGW(TAG, "Ping session timeout waiting for completion");
        // If still running, stop and cleanup
        esp_ping_stop(ping);
        esp_ping_delete_session(ping);
        return false;
    }

    // Success = at least one reply
    return g_ping_summary.success;
}

// control the Ethernet LED (GPIO45/46) to indicate link/activity
void eth_led_init(void)
{
    uint64_t mask = 0;

    if (hw.has_eth_led)
        mask |= (1ULL << hw.pin_eth_led);

    if (hw.has_wifi_led)
        mask |= (1ULL << hw.pin_wifi_led);

    if (mask == 0)
        return; // v2 hardware → no LEDs

    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = mask,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    if (hw.has_eth_led)
        gpio_set_level(hw.pin_eth_led, 0);

    if (hw.has_wifi_led)
        gpio_set_level(hw.pin_wifi_led, 0);
}

static void led_slow_blink_task(void *arg)
{
    gpio_num_t pin = (gpio_num_t)arg;

    while (1)
    {
        gpio_set_level(pin, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(pin, 0);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

static void led_fast_blink_task(void *arg)
{
    gpio_num_t pin = (gpio_num_t)arg;

    while (1)
    {
        gpio_set_level(pin, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(pin, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void led_double_blink_task(void *arg)
{
    gpio_num_t pin = (gpio_num_t)arg;

    while (1)
    {
        // Blink 1
        gpio_set_level(pin, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
        gpio_set_level(pin, 0);
        vTaskDelay(pdMS_TO_TICKS(120));

        // Blink 2
        gpio_set_level(pin, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
        gpio_set_level(pin, 0);

        // Pause
        vTaskDelay(pdMS_TO_TICKS(800));
    }
}

static void set_led_mode(led_mode_t mode, gpio_num_t pin, TaskHandle_t *task_handle, led_mode_t *current_mode)
{
    if (*current_mode == mode)
        return;

    *current_mode = mode;

    // Stop old task
    if (*task_handle)
    {
        vTaskDelete(*task_handle);
        *task_handle = NULL;
    }

    switch (mode)
    {
    case LED_OFF:
        gpio_set_level(pin, 0);
        break;

    case LED_SOLID:
        gpio_set_level(pin, 1);
        break;

    case LED_SLOW_BLINK:
        xTaskCreate(led_slow_blink_task, "led_slow", 2048, (void *)pin, 1, task_handle);
        break;

    case LED_FAST_BLINK:
        xTaskCreate(led_fast_blink_task, "led_fast", 2048, (void *)pin, 1, task_handle);
        break;

    case LED_DOUBLE_BLINK:
        xTaskCreate(led_double_blink_task, "led_double", 2048, (void *)pin, 1, task_handle);
        break;
    }
}

// Return true if Wi‑Fi is enabled in config (thread safe)
static bool wifi_manager_is_enabled(void)
{
    bool enabled = false;
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        enabled = (wifi_config.enable_wifi[0] != '\0' && strcmp(wifi_config.enable_wifi, "On") == 0);
        xSemaphoreGive(network_config_mutex);
    }
    else
    {
        LOGW("net_func", "wifi_manager_is_enabled: could not take network_config_mutex; assuming disabled");
    }
    return enabled;
}

void eth_led_set_mode(led_mode_t mode)
{
    if (!hw.has_eth_led)
        return;

    set_led_mode(mode, hw.pin_eth_led, &eth_led_task, &eth_led_mode);
}

void wifi_led_set_mode(led_mode_t mode)
{
    if (!hw.has_wifi_led)
        return;

    set_led_mode(mode, hw.pin_wifi_led, &wifi_led_task, &wifi_led_mode);
}

// Call from network_manager_task periodically
void network_manager_health_check(void)
{
    static int ping_fail_count = 0;
    const int max_ping_fail = 3;

    // If no active netif or no IP, try to recover
    if (!network_has_ip())
    {
        LOGW(TAG, "Health check: no network interface currently has an active IP address");

        // If ethernet is configured and present, try to restart it (guarded).
        if (hw.has_ethernet && ethernet_config_is_enabled())
        {
            LOGI(TAG, "Attempting Ethernet recovery: guarded restart");
            // attempt_guarded_eth_restart() will check link and defer teardown if busy, and will also trigger Wi‑Fi recovery if Ethernet is not healthy. This is preferred over a blind Wi‑Fi restart when Ethernet is configured, to avoid unnecessary Wi‑Fi disruptions when Ethernet is just slow to recover.
            attempt_guarded_eth_restart();
        }
        else
        {
            // Try Wi‑Fi reconnect if Wi‑Fi enabled in config
            if (wifi_manager_is_enabled())
            {
                LOGI(TAG, "Attempting Wi‑Fi reconnect (init + select mode)");
                if (wifi_manager_init() == ESP_OK)
                {
                    esp_err_t r = select_wifi_mode();
                    if (r != ESP_OK)
                        LOGW(TAG, "select_wifi_mode failed: %s", esp_err_to_name(r));
                    else
                        LOGI(TAG, "Wi‑Fi reconnect requested via select_wifi_mode");
                }
                else
                {
                    LOGW(TAG, "wifi_manager_init failed; cannot reconnect Wi‑Fi");
                }
            }
        }

        // After recovery attempt, force re-evaluation of config and services
        network_manager_request_actuals_update();
        return;
    }

    // If we have IP, ping gateway to ensure reachability
    if (!ping_gateway())
    {
        ping_fail_count++;
        LOGW(TAG, "Health check: ping gateway failed (%d/%d)", ping_fail_count, max_ping_fail);
        if (ping_fail_count >= max_ping_fail)
        {
            LOGE(TAG, "Gateway unreachable repeatedly — restarting network stack");
            ping_fail_count = 0;

            // Determine active interface and restart appropriately
            esp_netif_t *active = get_active_netif();

            if (hw.has_ethernet && active != NULL)
            {
                esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
                if (eth == active)
                {
                    LOGI(TAG, "Restarting Ethernet stack (guarded)");
                    attempt_guarded_eth_restart();
                }
                else
                {
                    // Active is not Ethernet; try Wi‑Fi recovery
                    LOGI(TAG, "Active interface is not Ethernet; attempting Wi‑Fi recovery");
                    if (wifi_manager_init() == ESP_OK)
                        select_wifi_mode();
                }
            }
            else
            {
                // No ethernet or not active: try Wi‑Fi recovery
                LOGI(TAG, "Attempting Wi‑Fi recovery");
                if (wifi_manager_init() == ESP_OK)
                    select_wifi_mode();
            }

            // Restart MQTT after network restart attempt
            MQTT_config_t cfg;
            mqtt_get_config_snapshot(&cfg);
            mqtt_stop();
            vTaskDelay(pdMS_TO_TICKS(300));
            mqtt_start(&cfg);
        }
    }
    else
    {
        // success: reset counter
        ping_fail_count = 0;
    }
}
