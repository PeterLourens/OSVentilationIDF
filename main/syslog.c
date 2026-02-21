/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "syslog.h"

const char *TAG = "syslog";

// Enable syslog when wifi network is connected
bool syslog_enabled = false;

void syslog_send_line(const char *line)
{
    if (!line || !*line)
    {
        ESP_LOGW(TAG, "Empty line, nothing to send");
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
        ESP_LOGE(TAG, "Invalid syslog port: %s", port_str);
        return;
    }

    if (strlen(ip) == 0)
    {
        ESP_LOGE(TAG, "Syslog IP not configured");
        return;
    }

    if (strlen(hostname) == 0)
        strcpy(hostname, "set hostame");

    if (strlen(appname) == 0)
        strcpy(appname, "set app name");

    if (strlen(device_id) == 0)
        strcpy(appname, "set device ID");

    /* ---- Build syslog message ---- */
    char *msg = NULL;
    int n = asprintf(&msg, "<13>%s %s: %s\n", hostname, appname, line);
    if (n < 0 || !msg)
    {
        ESP_LOGE(TAG, "asprintf failed");
        return;
    }

    /* ---- Create UDP socket ---- */
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        free(msg);
        return;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);

    if (!inet_aton(ip, &dest.sin_addr))
    {
        ESP_LOGE(TAG, "inet_aton failed for %s", ip);
        close(sock);
        free(msg);
        return;
    }

    /* ---- Send message ---- */
    int sent = sendto(sock, msg, strlen(msg), 0,
                      (struct sockaddr *)&dest, sizeof(dest));

    if (sent < 0)
        ESP_LOGE(TAG, "sendto failed: errno=%d", errno);
    else
        ESP_LOGD(TAG, "Sent %d bytes to syslog-ng", sent);

    close(sock);
    free(msg);
}

/* ---- Minimal UDP syslog sender (RFC3164-ish) ---- */
/*void syslog_send_line(const char *line)
{
    if (!line || !*line)
    {
        ESP_LOGW(TAG, "Empty line, nothing to send");
        return;
    }

    // Format syslog message: <PRI>TAG: message\n
    char *msg = NULL;
    int n = asprintf(&msg, "<13>esp32: %s\n", line);
    if (n < 0 || !msg)
    {
        ESP_LOGE(TAG, "asprintf failed");
        return;
    }

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        free(msg);
        return;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SYSLOG_SERVER_PORT);
    if (!inet_aton(SYSLOG_SERVER_IP, &dest.sin_addr))
    {
        ESP_LOGE(TAG, "inet_aton failed for %s", SYSLOG_SERVER_IP);
        close(sock);
        free(msg);
        return;
    }

    // Send message
    int sent = sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof(dest));
    if (sent < 0)
    {
        ESP_LOGE(TAG, "sendto failed: errno=%d", errno);
    }
    else
    {
        ESP_LOGD(TAG, "Sent %d bytes to syslog-ng", sent);
    }

    close(sock);
    free(msg);
}*/

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