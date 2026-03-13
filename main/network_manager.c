/*
    network_manager.c
    Unified network manager for WiFi + Ethernet

    Replaces task_eth, task_wifi, wifi_functions, eth_functions.
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

// Manager state
static volatile bool manager_running = false;

// Forward declarations
static void network_manager_task(void *pvParameters);
static void register_event_handlers(void);
static void unregister_event_handlers(void);
static void evaluate_config_and_apply(void);

// Public API implementations
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

// -------------------- Internal implementation --------------------
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT)
        return;

    switch (id)
    {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED");
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.wifi_connected = false;
            sm_actuals.wifi_error = true;
            xSemaphoreGive(statemachine_mutex);
        }
        update_syslog_state();
        break;
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.wifi_ap_mode = true;
            xSemaphoreGive(statemachine_mutex);
        }
        update_syslog_state();
        break;
    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.wifi_connected = true;
            sm_actuals.wifi_error = false;
            sm_actuals.wifi_ap_mode = false;
            xSemaphoreGive(statemachine_mutex);
        }
        update_syslog_state();
        network_manager_request_actuals_update();
    }
    else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP_EVENT_ETH_GOT_IP: " IPSTR, IP2STR(&event->ip_info.ip));

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, 10) == pdTRUE)
        {
            sm_actuals.eth_connected = true;
            xSemaphoreGive(statemachine_mutex);
        }

        update_syslog_state();                    // enable syslog
        network_manager_request_actuals_update(); // update actuals now
    }
}

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
        update_syslog_state();
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "ETHERNET_EVENT_DISCONNECTED");
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sm_actuals.eth_connected = false;
            xSemaphoreGive(statemachine_mutex);
        }
        update_syslog_state();
        break;
    default:
        break;
    }
}

static void register_event_handlers(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, &wifi_evt_inst));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL, &ip_evt_inst));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, NULL, &eth_evt_inst));
}

static void unregister_event_handlers(void)
{
    if (wifi_evt_inst)
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt_inst);
    if (ip_evt_inst)
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_evt_inst);
    if (eth_evt_inst)
        esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_evt_inst);
    wifi_evt_inst = ip_evt_inst = eth_evt_inst = NULL;
}

// Evaluate config and apply changes, enforcing DHCP flags
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

    // Update statemachine actuals for enabled flags under statemachine_mutex
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
            // Ensure DHCP client state matches config if netif exists
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
                    // static IP will be applied by select_wifi_mode()/wifi_manager_connect when selecting static path
                }
            }

            // Start or reconnect if not connected
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
    }

    // ---------- Ethernet ----------
    if (eth_enabled)
    {
        eth_manager_start(); // only init + handlers, no start

        esp_err_t r = ethernet_config_apply();
        if (r != ESP_OK)
            ESP_LOGW(TAG, "ethernet_config_apply returned %s", esp_err_to_name(r));
    }
    else
    {
        eth_stop();
    }
}

// Main manager task
static void network_manager_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Network manager starting");

    // Register event handlers before initializing netif to ensure we catch all events
    register_event_handlers();

    // Ensure TCP/IP stack initialized
    ESP_ERROR_CHECK(esp_netif_init());

    // Initialize both managers (safe to call multiple times)
    eth_manager_start();
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
