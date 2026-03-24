/*
    network_manager.c
    Unified network manager for WiFi + Ethernet + MQTT

    Single orchestrator for events, LEDs, statemachine, syslog.
    Copyright (C) 2023-2026 Peter Lourens
*/

#include "network_manager.h"

static const char *TAG = "net_mgr";

// Task handle and stack
static StackType_t network_manager_stack[5120];
static StaticTask_t network_manager_tcb;
static const size_t network_manager_stack_size = sizeof(network_manager_stack);

// Event handler instances
static esp_event_handler_instance_t wifi_evt_inst = NULL;
static esp_event_handler_instance_t ip_evt_inst = NULL;
static esp_event_handler_instance_t eth_evt_inst = NULL;
static esp_event_handler_instance_t mqtt_evt_inst = NULL;

// Manager state
static volatile bool manager_running = false;

// Forward declarations
static void network_manager_task(void *pvParameters);
static void register_event_handlers(void);
static void unregister_event_handlers(void);
static void evaluate_config_and_apply(void);

// -------------------- Public API --------------------

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
        ESP_LOGE(TAG, "network_manager_start: failed to create task");
        return false;
    }

    manager_running = true;
    LOGI(TAG, "network_manager_start: started (task=%p)", (void *)network_manager_task_handle);
    return true;
}

void network_manager_notify_config_change(void)
{
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

// -------------------- Event handlers --------------------

// Wi‑Fi events (STA + AP)
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT)
        return;

    switch (id)
    {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.wifi_connected = false;
            sm_actuals.wifi_error = false;
            sm_actuals.wifi_ap_mode = false;
            xSemaphoreGive(statemachine_mutex);
        }

        wifi_led_set_mode(LED_SLOW_BLINK); // enabled but not connected

        if (task_rgb_leds != NULL)
            xTaskNotifyGive(task_rgb_leds);

        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
        wifi_led_set_mode(LED_FAST_BLINK); // DHCP running
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
    {
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED");

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

            if (task_rgb_leds != NULL)
                xTaskNotifyGive(task_rgb_leds);

            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retry to connect to the AP (%d/%d)", s_retry_num, MAXIMUM_RETRY);
        }
        else
        {
            if (s_wifi_event_group)
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to AP after %d retries", MAXIMUM_RETRY);
        }

        update_syslog_state();
        break;
    }

    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "WIFI_EVENT_AP_START");

        wifi_led_set_mode(LED_SOLID); // AP mode = solid

        if (s_wifi_event_group)
            xEventGroupSetBits(s_wifi_event_group, AP_STARTED_BIT);

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.wifi_ap_mode = true;
            xSemaphoreGive(statemachine_mutex);
        }

        if (task_rgb_leds != NULL)
            xTaskNotifyGive(task_rgb_leds);

        update_syslog_state();
        break;

    case WIFI_EVENT_AP_STACONNECTED:
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
        break;
    }

    case WIFI_EVENT_AP_STADISCONNECTED:
    {
        wifi_event_ap_stadisconnected_t *event_disc = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event_disc->mac), event_disc->aid);
        break;
    }

    default:
        break;
    }
}

// IP events (Wi‑Fi + Ethernet)
static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base != IP_EVENT)
        return;

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;

    // Wi‑Fi STA got IP
    if (id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: " IPSTR, IP2STR(&event->ip_info.ip));

        wifi_led_set_mode(LED_DOUBLE_BLINK); // IP acquired

        extern int s_retry_num;
        s_retry_num = 0;

        if (s_wifi_event_group)
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.wifi_connected = true;
            sm_actuals.wifi_error = false;
            sm_actuals.wifi_ap_mode = false;
            xSemaphoreGive(statemachine_mutex);
        }

        if (task_rgb_leds != NULL)
            xTaskNotifyGive(task_rgb_leds);

        update_syslog_state();
        network_manager_request_actuals_update();
        return;
    }

    // Ethernet got IP
    if (id == IP_EVENT_ETH_GOT_IP)
    {
        ESP_LOGI(TAG, "IP_EVENT_ETH_GOT_IP: " IPSTR, IP2STR(&event->ip_info.ip));

        eth_led_set_mode(LED_DOUBLE_BLINK);

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            sm_actuals.eth_connected = true;

            snprintf(sm_actuals.eth_ip_addr, sizeof(sm_actuals.eth_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
            snprintf(sm_actuals.eth_netmask, sizeof(sm_actuals.eth_netmask), IPSTR, IP2STR(&event->ip_info.netmask));
            snprintf(sm_actuals.eth_gateway, sizeof(sm_actuals.eth_gateway), IPSTR, IP2STR(&event->ip_info.gw));

            esp_netif_dns_info_t dns;
            if (esp_netif_get_dns_info(eth_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK)
                snprintf(sm_actuals.eth_dns1, sizeof(sm_actuals.eth_dns1), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
            if (esp_netif_get_dns_info(eth_netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK)
                snprintf(sm_actuals.eth_dns2, sizeof(sm_actuals.eth_dns2), IPSTR, IP2STR(&dns.ip.u_addr.ip4));

            xSemaphoreGive(statemachine_mutex);
        }

        http_server_stop();
        http_server_start();

        update_syslog_state();
        network_manager_request_actuals_update();
        return;
    }
}

// Ethernet link events
static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    switch (id)
    {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "ETHERNET_EVENT_CONNECTED");

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.eth_connected = true;
            xSemaphoreGive(statemachine_mutex);
        }

        // Make Ethernet the default route as soon as link is up
        esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (eth)
        {
            esp_netif_set_default_netif(eth);
            ESP_LOGI(TAG, "ETH set as default route");
        }

        eth_led_set_mode(LED_FAST_BLINK);
        update_syslog_state();
        break;

    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "ETHERNET_EVENT_DISCONNECTED");

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.eth_connected = false;
            xSemaphoreGive(statemachine_mutex);
        }

        eth_led_set_mode(LED_SLOW_BLINK); // enabled but no link
        update_syslog_state();
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
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_set_connected(true);

        bool eth_up = false;
        bool wifi_up = false;

        if (statemachine_mutex &&
            xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            eth_up = sm_actuals.eth_connected;
            wifi_up = sm_actuals.wifi_connected || sm_actuals.wifi_ap_mode;
            xSemaphoreGive(statemachine_mutex);
        }

        if (eth_up)
        {
            eth_led_set_mode(LED_SOLID);
        }

        if (wifi_up)
        {
            wifi_led_set_mode(LED_SOLID);
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED:

        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");

        mqtt_set_connected(false);

        if (eth_is_connected())
        {
            eth_led_set_mode(LED_DOUBLE_BLINK); // IP OK, MQTT down
        }
        else if (wifi_manager_is_connected())
        {
            wifi_led_set_mode(LED_DOUBLE_BLINK);
        }
        break;

    default:
        break;
    }
}

// -------------------- Event registration --------------------
static void register_event_handlers(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, &wifi_evt_inst));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL, &ip_evt_inst));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, NULL, &eth_evt_inst));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        MQTT_EVENTS, ESP_EVENT_ANY_ID, &on_mqtt_event, NULL, &mqtt_evt_inst));
}

static void unregister_event_handlers(void)
{
    if (wifi_evt_inst)
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt_inst);
    if (ip_evt_inst)
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_evt_inst);
    if (eth_evt_inst)
        esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_evt_inst);
    if (mqtt_evt_inst)
        esp_event_handler_instance_unregister(MQTT_EVENTS, ESP_EVENT_ANY_ID, mqtt_evt_inst);

    wifi_evt_inst = ip_evt_inst = eth_evt_inst = mqtt_evt_inst = NULL;
}

// -------------------- Config evaluation --------------------

static void evaluate_config_and_apply(void)
{
    bool wifi_enabled = false;
    bool eth_enabled = false;
    bool wifi_dhcp = true;
    bool eth_dhcp = true;
    char hostname[64] = {0};

    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        wifi_enabled = (strcmp(wifi_config.enable_wifi, "On") == 0);
        eth_enabled = (strcmp(ethernet_config.enable_ethernet, "On") == 0);
        wifi_dhcp = (strcmp(wifi_config.enable_dhcp, "On") == 0);
        eth_dhcp = (strcmp(ethernet_config.enable_dhcp, "On") == 0);
        strncpy(hostname, wifi_config.hostname, sizeof(hostname) - 1);
        xSemaphoreGive(network_config_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "evaluate_config_and_apply: could not take network_config_mutex");
        return;
    }

    // Update statemachine actuals for enabled flags
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        sm_actuals.wifi_enabled = wifi_enabled;
        sm_actuals.ethernet_enabled = eth_enabled;
        sm_actuals.wifi_dhcp_enabled = wifi_dhcp;
        sm_actuals.eth_dhcp_enabled = eth_dhcp;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "evaluate_config_and_apply: could not take statemachine_mutex to update actuals");
    }

    // Apply hostname to both netifs if present
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

    // ---------- WiFi ----------
    if (wifi_enabled)
    {
        if (wifi_manager_init() == ESP_OK)
        {
            esp_netif_t *wifinet = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (wifinet)
            {
                if (wifi_dhcp)
                {
                    esp_err_t r = esp_netif_dhcpc_start(wifinet);
                    if (r != ESP_OK && r != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
                        ESP_LOGW(TAG, "esp_netif_dhcpc_start (wifi) returned %s", esp_err_to_name(r));
                }
                else
                {
                    esp_err_t r = esp_netif_dhcpc_stop(wifinet);
                    if (r != ESP_OK && r != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
                        ESP_LOGW(TAG, "esp_netif_dhcpc_stop (wifi) returned %s", esp_err_to_name(r));
                }
            }

            if (!wifi_manager_is_connected())
            {
                esp_err_t r = select_wifi_mode();
                if (r != ESP_OK)
                    ESP_LOGW(TAG, "select_wifi_mode returned %s", esp_err_to_name(r));
            }
        }
        else
        {
            ESP_LOGW(TAG, "wifi_manager_init failed or returned non-OK");
        }
    }
    else
    {
        wifi_manager_stop();
        wifi_led_set_mode(LED_OFF);
    }

    // ---------- Ethernet ----------
    if (eth_enabled)
    {
        eth_init(); // idempotent
        esp_err_t r = ethernet_config_apply();
        if (r != ESP_OK)
            ESP_LOGW(TAG, "ethernet_config_apply returned %s", esp_err_to_name(r));

        if (!sm_actuals.eth_connected)
            eth_led_set_mode(LED_SLOW_BLINK);
    }
    else
    {
        eth_stop();
        eth_led_set_mode(LED_OFF);
    }
}

// -------------------- Main manager task --------------------

static void network_manager_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Network manager starting");

    // Initialize Ethernet + WiFi LED GPIO
    eth_led_init();

    // Ensure TCP/IP stack initialized
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop if not already present
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
    }

    // Register event handlers
    register_event_handlers();

    // Initialize both managers (safe to call multiple times)
    eth_init();
    wifi_manager_init();

    manager_running = true;

    // Initial evaluation and authoritative actuals update
    evaluate_config_and_apply();
    sys_stats_update_actuals();

    for (;;)
    {
        // Wait for notification or timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));

        // Re-evaluate config and apply changes
        evaluate_config_and_apply();

        // Ask sys_stats (the authoritative source) to refresh global actuals
        sys_stats_update_actuals();
    }

    // Cleanup (never reached)
    unregister_event_handlers();
    manager_running = false;
    vTaskDelete(NULL);
}
