/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "wifi_functions.h"

static const char *TAG = "wifi_functions";

int s_retry_num = 0; // used by network_manager.c
static bool s_wifi_initialized = false;
char hostname[MEDIUM_CONFIG_ITEM] = {};

static bool cfg_str_is_on(const char *s)
{
    return (s && strcmp(s, "On") == 0);
}

void wifi_manager_stop(void)
{
    LOGI(TAG, "Stopping WiFi...");

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
    {
        LOGW(TAG, "esp_wifi_stop returned %s", esp_err_to_name(err));
    }

    esp_wifi_disconnect();

    if (s_wifi_event_group)
    {
        xEventGroupClearBits(s_wifi_event_group,
                             WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | AP_STARTED_BIT);
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

esp_err_t wifi_manager_init(void)
{
    if (s_wifi_initialized)
    {
        LOGD(TAG, "wifi_manager_init: already initialized");
        return ESP_OK;
    }

    if (s_wifi_event_group == NULL)
    {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL)
        {
            LOGE(TAG, "Failed to create WiFi event group");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_t *existing_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (existing_sta == NULL)
    {
        esp_netif_t *netif = esp_netif_create_default_wifi_sta();
        if (netif == NULL)
        {
            LOGE(TAG, "esp_netif_create_default_wifi_sta returned NULL");
            return ESP_FAIL;
        }
    }
    else
    {
        LOGI(TAG, "wifi_manager_init: reusing existing WIFI_STA_DEF netif %p", (void *)existing_sta);
    }

    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        strncpy(hostname, wifi_config.hostname, sizeof(hostname) - 1);
        xSemaphoreGive(network_config_mutex);
    }
    LOGI(TAG, "Setting WiFi hostname to: %s", hostname);
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif)
    {
        esp_err_t herr = esp_netif_set_hostname(sta_netif, hostname);
        if (herr != ESP_OK)
            LOGW(TAG, "esp_netif_set_hostname (STA) returned %s", esp_err_to_name(herr));
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT)
    {
        LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_wifi_initialized = true;
    LOGI(TAG, "WiFi manager initialized successfully");
    return ESP_OK;
}

esp_err_t select_wifi_mode(void)
{
    const char *TAG_LOCAL = "WiFi";
    esp_err_t err = ESP_OK;

    if (strcmp(wifi_config.enable_wifi, "Off") == 0)
    {
        LOGW(TAG_LOCAL, "WiFi disabled in config; not starting STA or AP");
        return ESP_ERR_INVALID_STATE;
    }

    if (strcmp(wifi_config.enable_dhcp, "On") == 0)
    {
        if (strlen(wifi_config.ssid) > 0 && strlen(wifi_config.wifi_password) > 0)
        {
            LOGI(TAG_LOCAL, "Attempting station mode with DHCP");
            err = wifi_manager_connect(&wifi_config);
            if (err == ESP_OK)
                return ESP_OK;
            LOGE(TAG_LOCAL, "Failed to connect in station mode with DHCP");
        }
        else
        {
            LOGW(TAG_LOCAL, "Invalid SSID or password for station mode");
        }
    }
    else
    {
        bool valid_config = true;
        if (!is_valid_ip_address(wifi_config.ip_address))
        {
            LOGE(TAG_LOCAL, "Invalid IP address: %s", wifi_config.ip_address);
            valid_config = false;
        }
        if (!is_valid_ip_address(wifi_config.subnet_mask))
        {
            LOGE(TAG_LOCAL, "Invalid subnet mask: %s", wifi_config.subnet_mask);
            valid_config = false;
        }
        if (!is_valid_ip_address(wifi_config.gateway))
        {
            LOGE(TAG_LOCAL, "Invalid gateway: %s", wifi_config.gateway);
            valid_config = false;
        }
        if (!is_valid_ip_address(wifi_config.primary_dns))
        {
            LOGE(TAG_LOCAL, "Invalid primary DNS: %s", wifi_config.primary_dns);
            valid_config = false;
        }
        if (strlen(wifi_config.secondary_dns) > 0 && !is_valid_ip_address(wifi_config.secondary_dns))
        {
            LOGE(TAG_LOCAL, "Invalid secondary DNS: %s", wifi_config.secondary_dns);
            valid_config = false;
        }

        if (valid_config && strlen(wifi_config.ssid) > 0 && strlen(wifi_config.wifi_password) > 0)
        {
            LOGI(TAG_LOCAL, "Attempting station mode with static IP");
            err = wifi_manager_connect(&wifi_config);
            if (err == ESP_OK)
                return ESP_OK;
            LOGE(TAG_LOCAL, "Failed to connect with static IP configuration");
        }
        else
        {
            LOGW(TAG_LOCAL, "Invalid static IP configuration or credentials");
        }
    }

    LOGW(TAG_LOCAL, "Falling back to AP mode");
    return wifi_manager_start_ap();
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
    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };

    strncpy((char *)wifi_cfg.sta.ssid, settings->ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, settings->wifi_password, sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

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

    xSemaphoreGive(network_config_mutex);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT)
    {
        LOGI(TAG, "Connected to SSID: %s", settings->ssid);
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

    esp_netif_t *ap_existing = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_existing == NULL)
    {
        esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
        if (!ap_netif)
        {
            LOGE(TAG, "Failed to create AP network interface");
            xSemaphoreGive(network_config_mutex);
            return ESP_FAIL;
        }
    }
    else
    {
        LOGI(TAG, "wifi_manager_start_ap: reusing existing WIFI_AP_DEF netif %p", (void *)ap_existing);
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
        LOGE(TAG, "Failed to set WiFi mode to AP: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        AP_STARTED_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(5000));
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

    return (is_connected);
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
