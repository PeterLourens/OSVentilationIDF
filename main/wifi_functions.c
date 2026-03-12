/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "wifi_functions.h"

static const char *TAG = "wifi_functions";
static int s_retry_num = 0;

// Event handler instances (kept so we can unregister if needed)
static esp_event_handler_instance_t wifi_evt_inst = NULL;
static esp_event_handler_instance_t ip_evt_inst = NULL;

// Initialization guard
static bool s_wifi_initialized = false;

// Hostname buffer (kept for compatibility with existing code)
char hostname[MEDIUM_CONFIG_ITEM] = {};

// Stop WiFi and clear state
void wifi_manager_stop(void)
{
    ESP_LOGI(TAG, "Stopping WiFi...");

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
    {
        ESP_LOGW(TAG, "esp_wifi_stop returned %s", esp_err_to_name(err));
    }

    esp_wifi_disconnect();

    if (s_wifi_event_group)
    {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | AP_STARTED_BIT);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        sm_actuals.wifi_connected = false;
        sm_actuals.wifi_error = false;
        sm_actuals.wifi_ap_mode = false;
        xSemaphoreGive(statemachine_mutex);
    }

    update_syslog_state();
}

// Idempotent initialization: safe to call multiple times
esp_err_t wifi_manager_init(void)
{
    if (s_wifi_initialized)
    {
        ESP_LOGD(TAG, "wifi_manager_init: already initialized");
        return ESP_OK;
    }

    // Create (or reuse) the WiFi event group and export it
    if (s_wifi_event_group == NULL)
    {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL)
        {
            ESP_LOGE(TAG, "Failed to create WiFi event group");
            return ESP_ERR_NO_MEM;
        }
    }

    // Defensive: ensure esp-netif is initialized. Prefer calling esp_netif_init() once from app_main(),
    // but tolerate calling it here and accept ESP_ERR_INVALID_STATE as OK.
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Ensure default STA netif exists (reuse if present)
    esp_netif_t *existing_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (existing_sta == NULL)
    {
        esp_netif_t *netif = esp_netif_create_default_wifi_sta();
        if (netif == NULL)
        {
            ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta returned NULL");
            return ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGI(TAG, "wifi_manager_init: reusing existing WIFI_STA_DEF netif %p", (void *)existing_sta);
    }

    // Create default event loop if not already present
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    // Read hostname from config under mutex and apply to STA netif if present
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        strncpy(hostname, wifi_config.hostname, sizeof(hostname) - 1);
        xSemaphoreGive(network_config_mutex);
    }
    ESP_LOGI(TAG, "Setting WiFi hostname to: %s", hostname);
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif)
    {
        esp_err_t herr = esp_netif_set_hostname(sta_netif, hostname);
        if (herr != ESP_OK)
            ESP_LOGW(TAG, "esp_netif_set_hostname (STA) returned %s", esp_err_to_name(herr));
    }

    // Initialize WiFi driver (safe to call multiple times)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT)
    {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Register event handlers once (store instances so we can unregister later if needed)
    if (wifi_evt_inst == NULL)
    {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_evt_inst));
    }
    if (ip_evt_inst == NULL)
    {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &ip_evt_inst));
    }

    s_wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    return ESP_OK;
}

// Select and start WiFi mode according to configuration
esp_err_t select_wifi_mode(void)
{
    const char *TAG_LOCAL = "WiFi";
    esp_err_t err = ESP_OK;

    if (strcmp(wifi_config.enable_wifi, "Off") == 0)
    {
        ESP_LOGW(TAG_LOCAL, "WiFi disabled in config; not starting STA or AP");
        return ESP_ERR_INVALID_STATE;
    }

    // DHCP path
    if (strcmp(wifi_config.enable_dhcp, "On") == 0)
    {
        if (strlen(wifi_config.ssid) > 0 && strlen(wifi_config.wifi_password) > 0)
        {
            ESP_LOGI(TAG_LOCAL, "Attempting station mode with DHCP");
            err = wifi_manager_connect(&wifi_config);
            if (err == ESP_OK)
                return ESP_OK;
            ESP_LOGE(TAG_LOCAL, "Failed to connect in station mode with DHCP");
        }
        else
        {
            ESP_LOGW(TAG_LOCAL, "Invalid SSID or password for station mode");
        }
    }
    else
    {
        // Static IP path: validate config
        bool valid_config = true;
        if (!is_valid_ip_address(wifi_config.ip_address))
        {
            ESP_LOGE(TAG_LOCAL, "Invalid IP address: %s", wifi_config.ip_address);
            valid_config = false;
        }
        if (!is_valid_ip_address(wifi_config.subnet_mask))
        {
            ESP_LOGE(TAG_LOCAL, "Invalid subnet mask: %s", wifi_config.subnet_mask);
            valid_config = false;
        }
        if (!is_valid_ip_address(wifi_config.gateway))
        {
            ESP_LOGE(TAG_LOCAL, "Invalid gateway: %s", wifi_config.gateway);
            valid_config = false;
        }
        if (!is_valid_ip_address(wifi_config.primary_dns))
        {
            ESP_LOGE(TAG_LOCAL, "Invalid primary DNS: %s", wifi_config.primary_dns);
            valid_config = false;
        }
        if (strlen(wifi_config.secondary_dns) > 0 && !is_valid_ip_address(wifi_config.secondary_dns))
        {
            ESP_LOGE(TAG_LOCAL, "Invalid secondary DNS: %s", wifi_config.secondary_dns);
            valid_config = false;
        }

        if (valid_config && strlen(wifi_config.ssid) > 0 && strlen(wifi_config.wifi_password) > 0)
        {
            ESP_LOGI(TAG_LOCAL, "Attempting station mode with static IP");
            err = wifi_manager_connect(&wifi_config);
            if (err == ESP_OK)
                return ESP_OK;
            ESP_LOGE(TAG_LOCAL, "Failed to connect with static IP configuration");
        }
        else
        {
            ESP_LOGW(TAG_LOCAL, "Invalid static IP configuration or credentials");
        }
    }

    ESP_LOGW(TAG_LOCAL, "Falling back to AP mode");
    return wifi_manager_start_ap();
}

// WiFi event handler (keeps previous behavior)
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                sm_actuals.wifi_connected = false;
                sm_actuals.wifi_error = false;
                xSemaphoreGive(statemachine_mutex);
            }
            if (task_rgb_leds != NULL)
            {
                xTaskNotifyGive(task_rgb_leds);
            }
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry_num < MAXIMUM_RETRY)
            {
                if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
                {
                    sm_actuals.wifi_connected = false;
                    sm_actuals.wifi_error = true;
                    xSemaphoreGive(statemachine_mutex);
                }
                if (task_rgb_leds != NULL)
                {
                    xTaskNotifyGive(task_rgb_leds);
                }
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

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP Started");
            if (s_wifi_event_group)
                xEventGroupSetBits(s_wifi_event_group, AP_STARTED_BIT);
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                sm_actuals.wifi_ap_mode = true;
                xSemaphoreGive(statemachine_mutex);
            }
            if (task_rgb_leds != NULL)
            {
                xTaskNotifyGive(task_rgb_leds);
            }
            update_syslog_state();
            break;

        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
        }
        break;

        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event_disc = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event_disc->mac), event_disc->aid);
        }
        break;

        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));

        update_syslog_state();

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
        {
            xTaskNotifyGive(task_rgb_leds);
        }
    }
}

// Connect in STA mode and optionally apply static IP if configured
esp_err_t wifi_manager_connect(Wifi_config_t *settings)
{
    if (!settings)
    {
        ESP_LOGE(TAG, "WiFi settings pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire WiFi config mutex");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;
    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };

    // Copy configuration under mutex protection
    strncpy((char *)wifi_cfg.sta.ssid, settings->ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, settings->wifi_password, sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    if (strcmp(settings->enable_dhcp, "Off") == 0)
    {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!netif)
        {
            ESP_LOGE(TAG, "Failed to get netif handle");
            result = ESP_FAIL;
            goto exit;
        }

        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));

        esp_netif_ip_info_t ip_info = {0};
        inet_pton(AF_INET, settings->ip_address, &ip_info.ip);
        inet_pton(AF_INET, settings->subnet_mask, &ip_info.netmask);
        inet_pton(AF_INET, settings->gateway, &ip_info.gw);

        if (esp_netif_set_ip_info(netif, &ip_info) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set static IP");
            result = ESP_FAIL;
            goto exit;
        }

        /* Configure DNS servers */
        esp_netif_dns_info_t dns_info = {0};
        inet_pton(AF_INET, settings->primary_dns, &dns_info.ip.u_addr.ip4);
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info));

        if (strlen(settings->secondary_dns) > 0)
        {
            inet_pton(AF_INET, settings->secondary_dns, &dns_info.ip.u_addr.ip4);
            ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info));
        }
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    // Release config mutex before waiting for connection, so event handler can set bits and update state
    xSemaphoreGive(network_config_mutex);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to SSID: %s", settings->ssid);
        return ESP_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", settings->ssid);
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Connection timeout");
    return ESP_ERR_TIMEOUT;

exit:
    xSemaphoreGive(network_config_mutex);
    return result;
}

// Start AP mode (creates AP netif only if needed)
esp_err_t wifi_manager_start_ap(void)
{
    if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire WiFi config mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Create default AP netif only if not present
    esp_netif_t *ap_existing = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_existing == NULL)
    {
        esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
        if (!ap_netif)
        {
            ESP_LOGE(TAG, "Failed to create AP network interface");
            xSemaphoreGive(network_config_mutex);
            return ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGI(TAG, "wifi_manager_start_ap: reusing existing WIFI_AP_DEF netif %p", (void *)ap_existing);
    }

    wifi_config_t wifi_cfg = {
        .ap = {
            .ssid = ESP_AP_SSID,
            .ssid_len = strlen(ESP_AP_SSID),
            .channel = ESP_AP_CHANNEL,
            .password = ESP_AP_PASS,
            .max_connection = ESP_AP_MAX_CONN,
            .authmode = strlen(ESP_AP_PASS) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false},
        },
    };

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set WiFi mode to AP: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, AP_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
    xSemaphoreGive(network_config_mutex);

    if (bits & AP_STARTED_BIT)
    {
        ESP_LOGI(TAG, "AP started successfully");
        ESP_LOGI(TAG, "SSID: %s", ESP_AP_SSID);
        ESP_LOGI(TAG, "Password: %s", strlen(ESP_AP_PASS) > 0 ? ESP_AP_PASS : "open");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "AP failed to start");
    return ESP_FAIL;
}

static bool cfg_str_is_on(const char *s)
{
    return (s && strcmp(s, "On") == 0);
}

// Check connection state using the event group
bool wifi_manager_is_connected(void)
{
    if (s_wifi_event_group == NULL)
    {
        ESP_LOGW(TAG, "WiFi event group not initialized");
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    bool is_connected = (bits & WIFI_CONNECTED_BIT) != 0;
    bool is_ap_mode = (bits & AP_STARTED_BIT) != 0;

    return (is_connected || is_ap_mode);
}

// Get IP string for current WiFi mode
esp_err_t wifi_manager_get_ip_info(char *ip_str, size_t max_len)
{
    if (!ip_str || max_len < 16)
    {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK)
    {
        return err;
    }

    esp_netif_t *netif = NULL;
    if (mode == WIFI_MODE_STA)
    {
        netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    }
    else if (mode == WIFI_MODE_AP)
    {
        netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    }

    if (!netif)
    {
        ESP_LOGE(TAG, "Failed to get netif handle");
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    err = esp_netif_get_ip_info(netif, &ip_info);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get IP info");
        return err;
    }

    snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

bool wifi_config_is_dhcp_enabled(void)
{
    bool dhcp = true;
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        dhcp = cfg_str_is_on(wifi_config.enable_dhcp);
        xSemaphoreGive(network_config_mutex);
    }
    else
    {
        LOGW(TAG, "Could not take network_config_mutex; assuming DHCP enabled");
    }
    return dhcp;
}