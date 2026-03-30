/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "syslog.h"

const char *TAG = "syslog";

// Enable syslog when wifi network is connected
bool syslog_enabled = false;

static bool syslog_network_ready(void)
{
    // Ethernet preferred
    if (hw.has_ethernet && eth_is_connected())
    {
        esp_netif_t *n = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (!n)
            return false;

        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(n, &ip) != ESP_OK)
            return false;

        return ip.ip.addr != 0; // Only true after DHCP completed
    }

    // WiFi fallback
    if (wifi_manager_is_connected())
    {
        esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!n)
            return false;

        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(n, &ip) != ESP_OK)
            return false;

        return ip.ip.addr != 0;
    }

    return false;
}

void syslog_send_line(const char *line)
{
    if (!syslog_network_ready())
    {
        return;
    }

    if (!line || !*line)
    {
        LOGW(TAG, "Empty line, nothing to send");
        return;
    }

    /* ---- Read syslog config safely ---- */
    char ip[MEDIUM_CONFIG_ITEM] = {0};
    char port_str[SMALL_CONFIG_ITEM] = {0};
    char hostname[MEDIUM_CONFIG_ITEM] = {0};
    char appname[MEDIUM_CONFIG_ITEM] = {0};
    char device_id[MEDIUM_CONFIG_ITEM] = {0};

    bool enabled = false;

    xSemaphoreTake(syslog_config_mutex, portMAX_DELAY);

    /* Master switch: "On" or "Off" */
    enabled = (strcmp(syslog_config.enable_syslog, "On") == 0);

    if (enabled)
    {
        strncpy(ip, syslog_config.syslog_ip, sizeof(ip) - 1);
        strncpy(port_str, syslog_config.syslog_port, sizeof(port_str) - 1);
        strncpy(hostname, syslog_config.syslog_hostname, sizeof(hostname) - 1);
        strncpy(appname, syslog_config.syslog_appname, sizeof(appname) - 1);
        strncpy(device_id, syslog_config.syslog_device_id, sizeof(device_id) - 1);
    }

    xSemaphoreGive(syslog_config_mutex);

    /* ---- Hard disable: exit immediately ---- */
    if (!enabled)
        return;

    /* ---- Validate config ---- */
    int port = atoi(port_str);
    if (port <= 0 || port > 65535)
    {
        LOGE(TAG, "Invalid syslog port: %s", port_str);
        return;
    }

    if (strlen(ip) == 0)
    {
        LOGE(TAG, "Syslog IP not configured");
        return;
    }

    if (strlen(hostname) == 0)
        snprintf(hostname, sizeof(hostname), "%s", "set hostname");

    if (strlen(appname) == 0)
        snprintf(appname, sizeof(appname), "%s", "set app name");

    if (strlen(device_id) == 0)
        snprintf(device_id, sizeof(device_id), "%s", "set device ID");

    /* ---- Build syslog message ---- */
    char *msg = NULL;
    int n = asprintf(&msg, "<13>%s %s: %s\n", hostname, appname, line);
    if (n < 0 || !msg)
    {
        LOGE(TAG, "asprintf failed");
        return;
    }

    /* ---- Create UDP socket ---- */
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        LOGE(TAG, "socket() failed: errno=%d", errno);
        free(msg);
        return;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);

    if (!inet_aton(ip, &dest.sin_addr))
    {
        LOGE(TAG, "inet_aton failed for %s", ip);
        close(sock);
        free(msg);
        return;
    }

    /* ---- Send message ---- */
    int sent = sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof(dest));

    if (sent < 0)
        LOGE(TAG, "sendto failed: errno=%d", errno);
    else
        LOGD(TAG, "Sent %d bytes to syslog-ng", sent);

    close(sock);
    free(msg);
}

/* Public: wait for IP on a given netif key ("WIFI_STA_DEF" or Ethernet key) */
bool network_wait_for_ip(int max_retries, int delay_ms, const char *netif_key)
{
    int retries = 0;
    esp_netif_ip_info_t ip;

    while (retries < max_retries)
    {
        esp_netif_t *netif = get_active_netif();

        if (netif)
        {
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK)
            {
                if (ip.ip.addr != 0)
                {
                    ESP_LOGI(TAG, "IP ready: " IPSTR, IP2STR(&ip.ip));
                    return true;
                }
            }
        }

        ESP_LOGW(TAG, "Waiting for IP on active interface... (%d/%d)",
                 retries + 1, max_retries);

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        retries++;
    }

    return false;
}

void syslog_enable(void)
{
    syslog_enabled = true;
}

void syslog_disable(void)
{
    syslog_enabled = false;
}