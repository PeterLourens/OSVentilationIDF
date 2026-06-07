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
    LOGI(TAG, "Stopping WiFi... (request received)");

    if (!s_wifi_initialized)
    {
        LOGI(TAG, "wifi_manager_stop: WiFi stop requested, but WiFi manager has never been initialized; no action taken");
        return;
    }

    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_STARTED)
    {
        LOGI(TAG, "wifi_manager_stop: WiFi driver not started, skip disconnect");
    }
#if defined(ESP_ERR_WIFI_CONN_LOST)
    else if (err != ESP_OK && err != ESP_ERR_WIFI_CONN_LOST)
#else
    else if (err != ESP_OK)
#endif
    {
        LOGW(TAG, "esp_wifi_disconnect returned %s", esp_err_to_name(err));
    }

    err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_STARTED)
    {
        LOGI(TAG, "wifi_manager_stop: WiFi driver not started, skip stop");
    }
    else if (err != ESP_OK)
    {
        LOGW(TAG, "esp_wifi_stop returned %s", esp_err_to_name(err));
    }

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

// select_wifi_mode: attempt STA only; do not perform automatic AP fallback.
// network_manager_task will decide whether to call wifi_manager_start_ap().
esp_err_t select_wifi_mode(void)
{
    const char *TAG_LOCAL = "WiFi";

    /* If Ethernet is connected, do not attempt STA or AP selection here.
       The network_manager will decide which action to take. */
    if (eth_is_connected())
    {
        LOGW(TAG_LOCAL, "select_wifi_mode: Ethernet is up → skipping WiFi selection");
        return ESP_OK;
    }

    /* Ensure WiFi is enabled in config */
    if (strcmp(wifi_config.enable_wifi, "Off") == 0)
    {
        LOGW(TAG_LOCAL, "select_wifi_mode: WiFi disabled in config");
        return ESP_ERR_INVALID_STATE;
    }

    /* If credentials are present, attempt STA only.
       Do NOT automatically fall back to AP here — caller (network_manager) will decide. */
    bool have_credentials =
        strlen(wifi_config.ssid) > 0 &&
        strlen(wifi_config.wifi_password) > 0;

    if (!have_credentials)
    {
        LOGW(TAG_LOCAL, "select_wifi_mode: no WiFi credentials configured; caller may start AP");
        return ESP_ERR_NOT_SUPPORTED;
    }

    LOGI(TAG_LOCAL, "select_wifi_mode: attempting WiFi STA");

    esp_err_t r = wifi_manager_connect(&wifi_config);
    if (r == ESP_OK)
    {
        LOGI(TAG_LOCAL, "select_wifi_mode: STA connected");
        return ESP_OK;
    }

    /* STA failed — return failure to caller so it can decide whether to start AP */
    LOGW(TAG_LOCAL, "select_wifi_mode: STA attempt failed (%s)", esp_err_to_name(r));
    return r;
}

// Wifi manager connect and AP functions
// Place these functions in wifi_functions.c (replace existing implementations)
esp_err_t wifi_manager_connect(Wifi_config_t *settings)
{
    if (!settings)
    {
        LOGE(TAG, "wifi_manager_connect: settings pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (network_config_mutex == NULL)
    {
        LOGE(TAG, "wifi_manager_connect: network_config_mutex is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    /* Copy settings under mutex to avoid holding it while blocking */
    Wifi_config_t local;
    memset(&local, 0, sizeof(local));
    if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        LOGE(TAG, "wifi_manager_connect: failed to take network_config_mutex");
        return ESP_ERR_TIMEOUT;
    }
    memcpy(&local, settings, sizeof(local));
    xSemaphoreGive(network_config_mutex);

    /* Ensure event group exists */
    if (s_wifi_event_group == NULL)
    {
        LOGE(TAG, "wifi_manager_connect: s_wifi_event_group not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear previous bits so wait is deterministic */
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    /* Prepare STA config */
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, local.ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, local.wifi_password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    /* Ensure STA netif exists (create if missing) */
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta_netif)
    {
        sta_netif = esp_netif_create_default_wifi_sta();
        if (!sta_netif)
        {
            LOGE(TAG, "wifi_manager_connect: WIFI_STA_DEF netif not found and could not be created");
            return ESP_ERR_NOT_FOUND;
        }
    }

    /* Configure static IP if requested (apply before start) */
    if (strcmp(local.enable_dhcp, "Off") == 0)
    {
        esp_err_t r = esp_netif_dhcpc_stop(sta_netif);
        if (r != ESP_OK && r != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
            LOGW(TAG, "wifi_manager_connect: dhcpc_stop -> %s", esp_err_to_name(r));

        esp_netif_ip_info_t ip_info = {0};
        if (inet_pton(AF_INET, local.ip_address, &ip_info.ip) != 1 ||
            inet_pton(AF_INET, local.subnet_mask, &ip_info.netmask) != 1 ||
            inet_pton(AF_INET, local.gateway, &ip_info.gw) != 1)
        {
            LOGE(TAG, "wifi_manager_connect: invalid static IP fields");
            return ESP_ERR_INVALID_ARG;
        }

        if (esp_netif_set_ip_info(sta_netif, &ip_info) != ESP_OK)
        {
            LOGE(TAG, "wifi_manager_connect: esp_netif_set_ip_info failed");
            return ESP_FAIL;
        }

        esp_netif_dns_info_t dns_info = {0};
        if (inet_pton(AF_INET, local.primary_dns, &dns_info.ip.u_addr.ip4) == 1)
        {
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        }
        if (strlen(local.secondary_dns) > 0 && inet_pton(AF_INET, local.secondary_dns, &dns_info.ip.u_addr.ip4) == 1)
        {
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &dns_info);
        }
    }
    else
    {
        esp_err_t r = esp_netif_dhcpc_start(sta_netif);
        if (r != ESP_OK && r != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
            LOGW(TAG, "wifi_manager_connect: dhcpc_start -> %s", esp_err_to_name(r));
    }

    /* Set mode and config */
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        LOGE(TAG, "wifi_manager_connect: esp_wifi_set_mode -> %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK)
    {
        LOGE(TAG, "wifi_manager_connect: esp_wifi_set_config -> %s", esp_err_to_name(err));
        return err;
    }

    /* Start Wi‑Fi (idempotent) */
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
    {
        LOGE(TAG, "wifi_manager_connect: esp_wifi_start -> %s", esp_err_to_name(err));
        return err;
    }

    /* Trigger connect (may be no-op if already connecting) */
    esp_err_t conn_err = esp_wifi_connect();
    if (conn_err != ESP_OK && conn_err != ESP_ERR_WIFI_NOT_STARTED)
    {
        LOGW(TAG, "wifi_manager_connect: esp_wifi_connect -> %s", esp_err_to_name(conn_err));
    }

    /* Wait for driver-level connected event first (10 s) */
    const TickType_t connect_wait = pdMS_TO_TICKS(10000);
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,  /* clear bits on exit */
        pdFALSE, /* wait for any */
        connect_wait);

    if (bits & WIFI_FAIL_BIT)
    {
        LOGW(TAG, "wifi_manager_connect: WIFI_FAIL_BIT set");
        return ESP_FAIL;
    }

    /* Poll for a valid IP (allow DHCP time) */
    const TickType_t ip_poll_total = pdMS_TO_TICKS(20000);
    const TickType_t ip_poll_interval = pdMS_TO_TICKS(500);
    TickType_t waited = 0;
    while (waited < ip_poll_total)
    {
        esp_netif_ip_info_t ipinfo;
        if (esp_netif_get_ip_info(sta_netif, &ipinfo) == ESP_OK && ipinfo.ip.addr != 0)
        {
            extern int s_retry_num;
            s_retry_num = 0;
            LOGI(TAG, "wifi_manager_connect: IP assigned: " IPSTR, IP2STR(&ipinfo.ip));
            return ESP_OK;
        }
        vTaskDelay(ip_poll_interval);
        waited += ip_poll_interval;
    }

    LOGW(TAG, "wifi_manager_connect: connection timeout to SSID '%s' (no IP after %d ms)", local.ssid, (int)pdTICKS_TO_MS(ip_poll_total));
    return ESP_ERR_TIMEOUT;
}

// wifi_manager_start_ap: start AP mode and return status. Caller must decide when to call this.
esp_err_t wifi_manager_start_ap(void)
{
    if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        LOGE(TAG, "wifi_manager_start_ap: failed to take network_config_mutex");
        return ESP_ERR_TIMEOUT;
    }

    /* Create or reuse AP netif */
    esp_netif_t *ap_existing = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_existing == NULL)
    {
        esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
        if (!ap_netif)
        {
            LOGE(TAG, "wifi_manager_start_ap: esp_netif_create_default_wifi_ap returned NULL");
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
                .required = false,
            },
        },
    };

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        LOGE(TAG, "wifi_manager_start_ap: esp_wifi_set_mode(AP) failed: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    if (err != ESP_OK)
    {
        LOGE(TAG, "wifi_manager_start_ap: esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        LOGE(TAG, "wifi_manager_start_ap: esp_wifi_start(AP) failed: %s", esp_err_to_name(err));
        xSemaphoreGive(network_config_mutex);
        return err;
    }

    /* Release mutex before waiting */
    xSemaphoreGive(network_config_mutex);

    /* Wait for AP_STARTED_BIT set by event handler */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        AP_STARTED_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(5000));

    if (bits & AP_STARTED_BIT)
    {
        LOGI(TAG, "wifi_manager_start_ap: AP started successfully (SSID=%s)", ESP_AP_SSID);
        return ESP_OK;
    }

    LOGE(TAG, "wifi_manager_start_ap: AP failed to start (timeout)");
    return ESP_FAIL;
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
