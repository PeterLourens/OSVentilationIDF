/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "wifi_functions.h"

static const char *TAG = "wifi_functions";
static int s_retry_num = 0;

char hostname[MEDIUM_CONFIG_ITEM] = {};

esp_err_t wifi_manager_init(void)
{
    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL)
    {
        LOGE(TAG, "Failed to create event group");
        vSemaphoreDelete(network_config_mutex);
        return ESP_ERR_NO_MEM;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default event loop if not created in main
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    // Create default WiFi station interface
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    // Set hostname BEFORE starting WiFi
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, 10) == pdTRUE)
    {
        LOGI(TAG, "Wifi_enable: %s, enable_dhcp: %s, hostname: %s, SSID: %s", wifi_config.enable_wifi, wifi_config.enable_dhcp, wifi_config.hostname, wifi_config.ssid);
        strncpy(hostname, wifi_config.hostname, sizeof(hostname));
        xSemaphoreGive(network_config_mutex);
    }
    LOGI(TAG, "Setting WiFi hostname to: %s", hostname);
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, hostname));

    // Create default WiFi station interface - Declared globally
    // esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    LOGI(TAG, "WiFi manager initialized successfully");
    return ESP_OK;
}

esp_err_t select_wifi_mode(void)
{
    const char *TAG = "WiFi";
    esp_err_t err = ESP_OK;

    if (strcmp(wifi_config.enable_wifi, "Off") == 0)
    {
        LOGW(TAG, "WiFi disabled in config; not starting STA or AP");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if DHCP is enabled
    if (strcmp(wifi_config.enable_dhcp, "On") == 0)
    {
        // Verify SSID and password for station mode
        if (strlen(wifi_config.ssid) > 0 && strlen(wifi_config.wifi_password) > 0)
        {
            LOGI(TAG, "Attempting station mode with DHCP");
            err = wifi_manager_connect(&wifi_config);
            if (err == ESP_OK)
            {
                return ESP_OK;
            }
            LOGE(TAG, "Failed to connect in station mode with DHCP");
        }
        else
        {
            LOGW(TAG, "Invalid SSID or password for station mode");
        }
    }
    else
    {
        // DHCP disabled - validate static IP configuration
        bool valid_config = true;

        if (!is_valid_ip_address(wifi_config.ip_address))
        {
            LOGE(TAG, "Invalid IP address: %s", wifi_config.ip_address);
            valid_config = false;
        }
        if (!is_valid_ip_address(wifi_config.subnet_mask))
        {
            LOGE(TAG, "Invalid subnet mask: %s", wifi_config.subnet_mask);
            valid_config = false;
        }
        if (!is_valid_ip_address(wifi_config.gateway))
        {
            LOGE(TAG, "Invalid gateway: %s", wifi_config.gateway);
            valid_config = false;
        }
        if (!is_valid_ip_address(wifi_config.primary_dns))
        {
            LOGE(TAG, "Invalid primary DNS: %s", wifi_config.primary_dns);
            valid_config = false;
        }
        if (strlen(wifi_config.secondary_dns) > 0 &&
            !is_valid_ip_address(wifi_config.secondary_dns))
        {
            LOGE(TAG, "Invalid secondary DNS: %s", wifi_config.secondary_dns);
            valid_config = false;
        }

        if (valid_config && strlen(wifi_config.ssid) > 0 &&
            strlen(wifi_config.wifi_password) > 0)
        {
            LOGI(TAG, "Attempting station mode with static IP");
            err = wifi_manager_connect(&wifi_config);
            if (err == ESP_OK)
            {
                return ESP_OK;
            }
            LOGE(TAG, "Failed to connect with static IP configuration");
        }
        else
        {
            LOGW(TAG, "Invalid static IP configuration or credentials");
        }
    }

    // If we get here, either configuration was invalid or connection failed
    LOGW(TAG, "Falling back to AP mode");
    return wifi_manager_start_ap();
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
            {
                sm_actuals.wifi_connected = false;
                sm_actuals.wifi_error = false;
                xSemaphoreGive(statemachine_mutex);
            }
            if (task_rgb_leds != NULL)
            {
                xTaskNotifyGive(task_rgb_leds);
            }
            else
            {
                LOGW(TAG, "task_rgb_leds is NULL; cannot notify of WiFi disconnect");
            }
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry_num < MAXIMUM_RETRY)
            {
                if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
                {
                    sm_actuals.wifi_connected = false;
                    sm_actuals.wifi_error = true;
                    xSemaphoreGive(statemachine_mutex);
                }
                if (task_rgb_leds != NULL)
                {
                    xTaskNotifyGive(task_rgb_leds);
                }
                else
                {
                    LOGW(TAG, "task_rgb_leds is NULL; cannot notify of WiFi disconnect");
                }
                esp_wifi_connect();
                s_retry_num++;
                LOGW(TAG, "Retry to connect to the AP (%d/%d)", s_retry_num, MAXIMUM_RETRY);
            }
            else
            {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                LOGE(TAG, "Failed to connect to AP after %d retries", MAXIMUM_RETRY);
            }

            // Disable syslog
            syslog_disable();

            break;

        case WIFI_EVENT_AP_START:
            LOGI(TAG, "AP Started");
            xEventGroupSetBits(s_wifi_event_group, AP_STARTED_BIT);
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
            {
                sm_actuals.wifi_ap_mode = true;
                xSemaphoreGive(statemachine_mutex);
            }
            if (task_rgb_leds != NULL)
            {
                xTaskNotifyGive(task_rgb_leds);
            }
            else
            {
                LOGW(TAG, "task_rgb_leds is NULL; cannot notify of WiFi disconnect");
            }
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            wifi_event_ap_stadisconnected_t *event_disc = (wifi_event_ap_stadisconnected_t *)event_data;
            LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event_disc->mac), event_disc->aid);
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));

        // Safe to enable syslog
        syslog_enable();

        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, 10) == pdTRUE)
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
        else
        {
            LOGW(TAG, "task_rgb_leds is NULL; cannot notify of WiFi disconnect");
        }
    }
}

esp_err_t wifi_manager_connect(Wifi_config_t *settings)
{
    if (!settings)
    {
        LOGE(TAG, "WiFi settings pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        LOGE(TAG, "Failed to acquire WiFi config mutex");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };

    // Copy configuration under mutex protection
    strncpy((char *)wifi_config.sta.ssid, settings->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, settings->wifi_password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    if (strcmp(settings->enable_dhcp, "Off") == 0)
    {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!netif)
        {
            LOGE(TAG, "Failed to get netif handle");
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
            LOGE(TAG, "Failed to set static IP");
            result = ESP_FAIL;
            goto exit;
        }

        // Configure DNS servers
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

    // Wait for connection with mutex released
    xSemaphoreGive(network_config_mutex);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT)
    {
        LOGI(TAG, "Connected to SSID: %s", settings->ssid);
        // update_hostname();
        return ESP_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        LOGE(TAG, "Failed to connect to SSID: %s", settings->ssid);
        return ESP_FAIL;
    }

    LOGE(TAG, "Connection timeout");
    return ESP_ERR_TIMEOUT;

exit:
    xSemaphoreGive(network_config_mutex);
    return result;
}

esp_err_t wifi_manager_start_ap(void)
{
    if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        LOGE(TAG, "Failed to acquire WiFi config mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Create default AP network interface
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif)
    {
        LOGE(TAG, "Failed to create AP network interface");
        xSemaphoreGive(network_config_mutex);
        return ESP_FAIL;
    }

    // Configure AP settings
    wifi_config_t wifi_config = {
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

    // Set WiFi mode to AP
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to set WiFi mode to AP: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    // Set AP configuration
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    // Start WiFi
    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    // Wait for AP to start
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, AP_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));

    xSemaphoreGive(network_config_mutex);

    if (bits & AP_STARTED_BIT)
    {
        LOGI(TAG, "AP started successfully");
        LOGI(TAG, "SSID: %s", ESP_AP_SSID);
        LOGI(TAG, "Password: %s", strlen(ESP_AP_PASS) > 0 ? ESP_AP_PASS : "open");
        return ESP_OK;
    }

    LOGE(TAG, "AP failed to start");
    return ESP_FAIL;
}

bool wifi_manager_is_connected(void)
{
    if (s_wifi_event_group == NULL)
    {
        LOGW(TAG, "WiFi event group not initialized");
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    bool is_connected = (bits & WIFI_CONNECTED_BIT) != 0;
    bool is_ap_mode = (bits & AP_STARTED_BIT) != 0;

    // Consider either station mode connection or AP mode as "connected"
    return (is_connected || is_ap_mode);
}

esp_err_t wifi_manager_get_ip_info(char *ip_str, size_t max_len)
{
    if (!ip_str || max_len < 16)
    { // Minimum length for IPv4 string (xxx.xxx.xxx.xxx)
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
        LOGE(TAG, "Failed to get netif handle");
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    err = esp_netif_get_ip_info(netif, &ip_info);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to get IP info");
        return err;
    }

    snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}