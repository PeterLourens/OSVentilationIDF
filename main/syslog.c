#include "syslog.h"

const char *TAG = "syslog";

// Enable syslog when wifi network is connected
bool syslog_enabled = false;

/* ---- Minimal UDP syslog sender (RFC3164-ish) ---- */
void syslog_send_line(const char *line)
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
}

/* Public: wait for IP on a given netif key ("WIFI_STA_DEF" or Ethernet key) */
bool network_wait_for_ip(int max_retries, int delay_ms, const char *netif_key)
{
    if (!netif_key)
        netif_key = "WIFI_STA_DEF";

    int retries = 0;

    while (retries < max_retries)
    {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey(netif_key), &ip) == ESP_OK)
        {
            if (ip.ip.addr != 0)
            {
                ESP_LOGD(TAG, "IP ready: " IPSTR, IP2STR(&ip.ip));
                return true;
            }
        }
        ESP_LOGW(TAG, "Waiting for IP on %s... (%d/%d)", netif_key, retries + 1, max_retries);
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