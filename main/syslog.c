/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "syslog.h"

const char *TAG = "syslog";

typedef struct
{
    char *line; // heap-allocated
    size_t len;
} syslog_queue_item_t;

// Static state
static QueueHandle_t s_syslog_queue = NULL;

/// Forward declarations
static void syslog_worker_task(void *pvParameters);
static int bind_socket_to_active_interface(int sock);

// Enable syslog when wifi network is connected
bool syslog_enabled = false;

// Diagnostic/backoff state for syslog sends. We keep a single backoff timestamp to avoid tight retry loops when the network or remote syslog server is unreachable. */
static int64_t s_last_syslog_fail_ms = 0;
#define SYSLOG_RETRY_BACKOFF_MS 5000

static int bind_socket_to_active_interface(int sock)
{
    esp_netif_t *netif = get_active_netif();
    if (!netif)
    {
        ESP_LOGW(TAG, "bind_socket_to_active_interface: no active netif");
        return -1;
    }

    esp_netif_ip_info_t ipinfo;
    if (esp_netif_get_ip_info(netif, &ipinfo) != ESP_OK)
    {
        ESP_LOGW(TAG, "bind_socket_to_active_interface: failed to get ip info");
        return -1;
    }

    if (ipinfo.ip.addr == 0)
    {
        ESP_LOGW(TAG, "bind_socket_to_active_interface: active netif has no IP");
        return -1;
    }

    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr.s_addr = ipinfo.ip.addr;

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) != 0)
    {
        ESP_LOGW(TAG, "bind_socket_to_active_interface: bind failed: errno=%d (%s)", errno, strerror(errno));
        return -1;
    }

    ESP_LOGD(TAG, "bind_socket_to_active_interface: bound to %s", inet_ntoa(*(struct in_addr *)&ipinfo.ip.addr));
    return 0;
}

static ssize_t safe_syslog_send(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest, socklen_t destlen)
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
        ESP_LOGD(TAG, "safe_syslog_send: skipping send due to backoff (elapsed=%lldms)", (long long)(now_ms - s_last_syslog_fail_ms));
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

    // Treat common transient errors specially; apply backoff but keep socket open.
    if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR)
    {
        s_last_syslog_fail_ms = now_ms;
        return -1;
    }

    // For other errors, apply backoff and return failure. The caller (which creates and closes the socket per message) will close the socket.
    s_last_syslog_fail_ms = now_ms;
    return -1;
}

static void syslog_worker_task(void *pvParameters)
{
    (void)pvParameters;
    syslog_queue_item_t item;

    for (;;)
    {
        if (s_syslog_queue == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (xQueueReceive(s_syslog_queue, &item, portMAX_DELAY) != pdTRUE)
            continue;

        if (!item.line)
            continue;

        /* Snapshot config */
        char ip[MEDIUM_CONFIG_ITEM] = {0};
        char port_str[SMALL_CONFIG_ITEM] = {0};
        char hostname_local[MEDIUM_CONFIG_ITEM] = {0};
        char appname_local[MEDIUM_CONFIG_ITEM] = {0};
        bool enabled = false;

        if (syslog_config_mutex && xSemaphoreTake(syslog_config_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            enabled = (strcmp(syslog_config.enable_syslog, "On") == 0);
            if (enabled)
            {
                strncpy(ip, syslog_config.syslog_ip, sizeof(ip) - 1);
                strncpy(port_str, syslog_config.syslog_port, sizeof(port_str) - 1);
                strncpy(hostname_local, syslog_config.syslog_hostname, sizeof(hostname_local) - 1);
                strncpy(appname_local, syslog_config.syslog_appname, sizeof(appname_local) - 1);
            }
            xSemaphoreGive(syslog_config_mutex);
        }
        else
        {
            ESP_LOGW(TAG, "syslog_worker: could not take syslog_config_mutex; skipping send");
            free(item.line);
            continue;
        }

        if (!enabled)
        {
            free(item.line);
            continue;
        }

        int port = atoi(port_str);
        if (port <= 0 || port > 65535)
        {
            ESP_LOGE(TAG, "syslog_worker: invalid port '%s', dropping message", port_str);
            free(item.line);
            continue;
        }

        if (strlen(ip) == 0)
        {
            ESP_LOGE(TAG, "syslog_worker: syslog IP empty, dropping message");
            free(item.line);
            continue;
        }

        // Build syslog message
        char *msg = NULL;
        int n = asprintf(&msg, "<13>%s %s: %s\n", hostname_local[0] ? hostname_local : "set-hostname", appname_local[0] ? appname_local : "set-app", item.line);
        if (n < 0 || !msg)
        {
            ESP_LOGE(TAG, "syslog_worker: asprintf failed");
            free(item.line);
            continue;
        }

        // Prepare destination address
        struct sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port);
        if (!inet_aton(ip, &dest.sin_addr))
        {
            ESP_LOGE(TAG, "syslog_worker: inet_aton failed for %s", ip);
            free(msg);
            free(item.line);
            continue;
        }

        if (!network_has_ip())
        {
            ESP_LOGW(TAG, "syslog_worker: no active network interface; dropping syslog message");
            free(msg);
            free(item.line);
            continue;
        }

        // Create UDP socket
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "syslog_worker: socket() failed: errno=%d (%s)", errno, strerror(errno));
            free(msg);
            free(item.line);
            vTaskDelay(pdMS_TO_TICKS(SYSLOG_RETRY_DELAY_MS));
            continue;
        }

        // Bind socket to active interface IP so outgoing packets use that interface
        if (bind_socket_to_active_interface(sock) != 0)
        {
            ESP_LOGW(TAG, "syslog_worker: failed to bind socket to active interface; continuing (socket will use default route)");
            // Not fatal: continue and attempt send
        }

        // Try to send with a few retries for transient errors
        ssize_t sent = -1;
        for (int attempt = 0; attempt < SYSLOG_SEND_RETRIES; ++attempt)
        {
            sent = safe_syslog_send(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof(dest));
            if (sent >= 0)
                break;
            vTaskDelay(pdMS_TO_TICKS(SYSLOG_RETRY_DELAY_MS));
        }

        if (sent < 0)
        {
            ESP_LOGE(TAG, "syslog_worker: send failed after %d attempts", SYSLOG_SEND_RETRIES);
        }
        else
        {
            ESP_LOGD(TAG, "syslog_worker: sent %d bytes", (int)sent);
            // Notify network manager that Ethernet activity occurred
            net_mgr_notify_eth_activity();
        }

        close(sock);
        free(msg);
        free(item.line);
    }
}

void syslog_queue_init(void)
{
    if (s_syslog_queue != NULL)
        return;

    s_syslog_queue = xQueueCreate(SYSLOG_QUEUE_LENGTH, sizeof(syslog_queue_item_t));
    if (s_syslog_queue == NULL)
    {
        ESP_LOGE(TAG, "syslog_queue_init: failed to create queue");
        return;
    }

    BaseType_t r = xTaskCreate(syslog_worker_task, "syslog_worker", SYSLOG_WORKER_STACK / sizeof(StackType_t), NULL, SYSLOG_WORKER_PRIO, &task_syslog);

    if (r != pdPASS)
    {
        ESP_LOGE(TAG, "syslog_queue_init: failed to create worker task");
        vQueueDelete(s_syslog_queue);
        s_syslog_queue = NULL;
        task_syslog = NULL;
    }
    else
    {
        ESP_LOGI(TAG, "syslog_queue_init: worker started");
    }
}

void syslog_queue_deinit(void)
{
    if (task_syslog)
    {
        vTaskDelete(task_syslog);
        task_syslog = NULL;
    }

    if (s_syslog_queue)
    {
        syslog_queue_item_t item;
        while (xQueueReceive(s_syslog_queue, &item, 0) == pdTRUE)
        {
            if (item.line)
                free(item.line);
        }
        vQueueDelete(s_syslog_queue);
        s_syslog_queue = NULL;
    }
}

void syslog_enqueue_line(const char *line)
{
    if (!line || !*line)
        return;

    if (s_syslog_queue == NULL)
    {
        // Queue not initialized: fall back to synchronous send
        ESP_LOGW(TAG, "syslog_enqueue_line: queue not initialized, falling back to direct send");
        // call original synchronous implementation below
        syslog_send_line(line);
        return;
    }

    size_t len = strnlen(line, SYSLOG_MAX_LINE_LEN);
    char *copy = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy)
    {
        ESP_LOGE(TAG, "syslog_enqueue_line: heap_caps_malloc failed");
        return;
    }
    memcpy(copy, line, len);
    copy[len] = '\0';

    syslog_queue_item_t item = {.line = copy, .len = len};

    if (xQueueSend(s_syslog_queue, &item, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "syslog_enqueue_line: queue full, dropping message");
        heap_caps_free(copy);
    }
}

void syslog_send_line(const char *line)
{
    // If queue exists, enqueue and return quickly
    if (s_syslog_queue != NULL)
    {
        syslog_enqueue_line(line);
        return;
    }

    if (!network_has_ip())
    {
        LOGI(TAG, "syslog_send_line: network not ready, skipping send");
        return;
    }

    if (!line || !*line)
    {
        LOGW(TAG, "syslog_send_line: empty line, nothing to send");
        return;
    }

    // Read syslog config safely
    char ip[MEDIUM_CONFIG_ITEM] = {0};
    char port_str[SMALL_CONFIG_ITEM] = {0};
    char hostname_local[MEDIUM_CONFIG_ITEM] = {0};
    char appname_local[MEDIUM_CONFIG_ITEM] = {0};
    char device_id_local[MEDIUM_CONFIG_ITEM] = {0};

    bool enabled = false;

    xSemaphoreTake(syslog_config_mutex, portMAX_DELAY);

    enabled = (strcmp(syslog_config.enable_syslog, "On") == 0);

    if (enabled)
    {
        strncpy(ip, syslog_config.syslog_ip, sizeof(ip) - 1);
        strncpy(port_str, syslog_config.syslog_port, sizeof(port_str) - 1);
        strncpy(hostname_local, syslog_config.syslog_hostname, sizeof(hostname_local) - 1);
        strncpy(appname_local, syslog_config.syslog_appname, sizeof(appname_local) - 1);
        strncpy(device_id_local, syslog_config.syslog_device_id, sizeof(device_id_local) - 1);
    }

    xSemaphoreGive(syslog_config_mutex);

    if (!enabled)
        return;

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

    if (strlen(hostname_local) == 0)
        snprintf(hostname_local, sizeof(hostname_local), "%s", "set hostname");

    if (strlen(appname_local) == 0)
        snprintf(appname_local, sizeof(appname_local), "%s", "set app name");

    if (strlen(device_id_local) == 0)
        snprintf(device_id_local, sizeof(device_id_local), "%s", "set device ID");

    size_t msg_len = strlen(hostname_local) + strlen(appname_local) + strlen(line) + 64;
    char *msg = heap_caps_malloc(msg_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!msg)
    {
        LOGE(TAG, "syslog_send_line: heap_caps_malloc failed for %zu bytes", msg_len);
        return;
    }

    int n = snprintf(msg, msg_len, "<13>%s %s: %s\n", hostname_local, appname_local, line);
    if (n < 0 || (size_t)n >= msg_len)
    {
        LOGE(TAG, "syslog_send_line: snprintf failed or truncated");
        heap_caps_free(msg);
        return;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "socket() failed: errno=%d (%s)", errno, strerror(errno));
        heap_caps_free(msg);
        return;
    }

    // Bind to active interface if possible
    if (bind_socket_to_active_interface(sock) != 0)
    {
        ESP_LOGW(TAG, "syslog_send_line: failed to bind socket to active interface; continuing");
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);

    if (!inet_aton(ip, &dest.sin_addr))
    {
        LOGE(TAG, "inet_aton failed for %s", ip);
        close(sock);
        heap_caps_free(msg);
        return;
    }

    ssize_t sent = safe_syslog_send(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof(dest));

    if (sent < 0)
    {
        LOGE(TAG, "syslog: send failed (see earlier diagnostics)");
    }
    else
    {
        LOGD(TAG, "Sent %d bytes to syslog-ng", (int)sent);
        // Notify network manager that Ethernet activity occurred
        net_mgr_notify_eth_activity();
    }

    close(sock);
    heap_caps_free(msg);
}

// Public: wait for IP on a given netif key ("WIFI_STA_DEF" or Ethernet key)
// Wait for IP on a specific netif key (if netif_key==NULL, wait for any active netif).
bool network_wait_for_ip(int max_retries, int delay_ms, const char *netif_key)
{
    int retries = 0;
    esp_netif_ip_info_t ip;

    while (retries < max_retries)
    {
        esp_netif_t *netif = NULL;

        if (netif_key && netif_key[0] != '\0')
            netif = esp_netif_get_handle_from_ifkey(netif_key);
        else
            netif = get_active_netif();

        if (netif)
        {
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK)
            {
                if (ip.ip.addr != 0)
                {
                    ESP_LOGI(TAG, "IP ready on %s: " IPSTR, netif_key ? netif_key : "active", IP2STR(&ip.ip));
                    return true;
                }
            }
        }

        ESP_LOGW(TAG, "Waiting for IP on %s... (%d/%d)",
                 netif_key ? netif_key : "active interface", retries + 1, max_retries);

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