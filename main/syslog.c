/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "syslog.h"

const char *TAG = "syslog";

// Enable syslog when wifi network is connected
bool syslog_enabled = false;

/* Diagnostic/backoff state for syslog sends.
   We keep a single backoff timestamp to avoid tight retry loops when the
   network or remote syslog server is unreachable. */
static int64_t s_last_syslog_fail_ms = 0;
#define SYSLOG_RETRY_BACKOFF_MS 5000

// Enable syslog when wifi network is connected
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

/*
 * safe_syslog_send
 *
 * Non-blocking diagnostic wrapper around sendto() tailored for the existing
 * syslog implementation which creates a transient UDP socket per message.
 *
 * - Applies a short backoff when repeated failures occur to avoid tight retry loops.
 * - Uses MSG_DONTWAIT to avoid blocking in small-stack contexts.
 * - Logs errno and strerror for diagnostics.
 *
 * Returns: number of bytes sent on success (>=0), -1 on failure (errno preserved).
 */
static ssize_t safe_syslog_send(int sockfd, const void *buf, size_t len, int flags,
                                const struct sockaddr *dest, socklen_t destlen)
{
    if (sockfd < 0)
    {
        ESP_LOGD(TAG, "safe_syslog_send: invalid socket fd=%d", sockfd);
        return -1;
    }

    int64_t now_ms = esp_timer_get_time() / 1000LL;
    if (s_last_syslog_fail_ms != 0 &&
        (now_ms - s_last_syslog_fail_ms) < SYSLOG_RETRY_BACKOFF_MS)
    {
        ESP_LOGD(TAG, "safe_syslog_send: skipping send due to backoff (elapsed=%lldms)",
                 (long long)(now_ms - s_last_syslog_fail_ms));
        return -1;
    }

    ssize_t r = sendto(sockfd, buf, len, flags | MSG_DONTWAIT, dest, destlen);
    if (r >= 0)
    {
        s_last_syslog_fail_ms = 0;
        return r;
    }

    int e = errno;
    ESP_LOGW(TAG, "syslog sendto failed: fd=%d errno=%d (%s)", sockfd, e, strerror(e));

    /* Treat common transient errors specially; apply backoff but keep socket open. */
    if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR)
    {
        s_last_syslog_fail_ms = now_ms;
        return -1;
    }

    /* For other errors, apply backoff and return failure. The caller (which
       creates and closes the socket per message) will close the socket. */
    s_last_syslog_fail_ms = now_ms;
    return -1;
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

    // Read syslog config safely
    char ip[MEDIUM_CONFIG_ITEM] = {0};
    char port_str[SMALL_CONFIG_ITEM] = {0};
    char hostname[MEDIUM_CONFIG_ITEM] = {0};
    char appname[MEDIUM_CONFIG_ITEM] = {0};
    char device_id[MEDIUM_CONFIG_ITEM] = {0};

    bool enabled = false;

    xSemaphoreTake(syslog_config_mutex, portMAX_DELAY);

    // Master switch: "On" or "Off"
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

    // Hard disable: exit immediately
    if (!enabled)
        return;

    // Validate config
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

    // Build syslog message
    char *msg = NULL;
    int n = asprintf(&msg, "<13>%s %s: %s\n", hostname, appname, line);
    if (n < 0 || !msg)
    {
        LOGE(TAG, "asprintf failed");
        return;
    }

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "socket() failed: errno=%d (%s)", errno, strerror(errno));
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

    // Send message using diagnostic wrapper
    ssize_t sent = safe_syslog_send(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof(dest));

    if (sent < 0)
        LOGE(TAG, "syslog: send failed (see earlier diagnostics)");
    else
        LOGD(TAG, "Sent %d bytes to syslog-ng", (int)sent);

    close(sock);
    free(msg);
}

// Public: wait for IP on a given netif key ("WIFI_STA_DEF" or Ethernet key)
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