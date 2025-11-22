#include "general.h"

esp_err_t init_nvs(void)
{
    const char *TAG = "Storage";
    esp_err_t err = ESP_OK;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS needs erase, retrying...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS (%s)", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}

esp_err_t init_littlefs(void)
{
    const char *TAG = "Storage";
    esp_err_t err = ESP_OK;

    const esp_vfs_littlefs_conf_t conf = {
        .base_path = "/data",
        .partition_label = "storage",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };

    err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used = 0;

    err = esp_littlefs_info(conf.partition_label, &total, &used);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get LittleFS partition info (%s)", esp_err_to_name(err));
        esp_vfs_littlefs_unregister(conf.partition_label);
        return err;
    }

    ESP_LOGI(TAG, "LittleFS mounted. Total: %d bytes, Used: %d bytes", total, used);

    DIR *dir = opendir("/data");
    if (!dir)
    {
        ESP_LOGE("LITTLEFS", "Failed to open /data");
        return ESP_FAIL;
    }

    struct dirent *entry;
    char full_path[270];
    struct stat st;

    while ((entry = readdir(dir)) != NULL)
    {
        snprintf(full_path, sizeof(full_path), "/data/%s", entry->d_name);

        if (stat(full_path, &st) == 0)
        {
            ESP_LOGI("LITTLEFS", "Found: %s (%ld bytes)", entry->d_name, st.st_size);
        }
        else
        {
            ESP_LOGW("LITTLEFS", "Found: %s (size unknown)", entry->d_name);
        }
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t read_file(const char *path, char *buffer, size_t bufferSize)
{
    static const char *TAG = "settings";

    if (!path || !buffer || bufferSize == 0)
    {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "rb"); // Use binary mode for safety
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return ESP_FAIL;
    }

    size_t len = fread(buffer, 1, bufferSize - 1, file); // Leave space for '\0'
    buffer[len] = '\0';                                  // Null-terminate
    fclose(file);

    if (len == 0)
    {
        ESP_LOGW(TAG, "Read 0 bytes from file: %s", path);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Read %zu bytes from %s", len, path);
    return ESP_OK;
}

bool is_valid_ip_address(const char *ip_address)
{
    if (!ip_address || strlen(ip_address) == 0)
    {
        return false;
    }
    struct in_addr addr;
    return inet_pton(AF_INET, ip_address, &addr) == 1;
}

esp_err_t create_mutexes(void)
{
    const char *TAG = "Mutexes";

    // Mutex for network settings
    network_config_mutex = xSemaphoreCreateMutex();
    if (network_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi config mutex");
    }

    // Mutex for time settings
    time_config_mutex = xSemaphoreCreateMutex();
    if (time_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi config mutex");
    }
    return ESP_OK;

    // Mutex for time settings
    lock_valve_move_mutex = xSemaphoreCreateMutex();
    if (lock_valve_move_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi config mutex");
    }
    return ESP_OK;
}

/* ========== Ping to gateway functions ========== */
void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    // const char *TAG = "Ping";
    uint32_t transmitted = 0, received = 0, duration = 0, avg_time = 0;

    // Profiles available in ESP‑IDF 5.5.1
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &duration, sizeof(duration));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &avg_time, sizeof(avg_time));

    int loss_percent = 0;
    if (transmitted > 0)
    {
        loss_percent = (int)((transmitted - received) * 100 / transmitted);
    }

    g_ping_summary.transmitted = (int)transmitted;
    g_ping_summary.received = (int)received;
    g_ping_summary.loss_percent = loss_percent;
    g_ping_summary.avg_time_ms = (int)avg_time;
    g_ping_summary.duration_ms = (int)duration;
    g_ping_summary.success = (received > 0);

    /*ESP_LOGI(TAG, "Ping finished: tx=%d rx=%d loss=%d%% avg_rtt=%d ms duration=%d ms",
             g_ping_summary.transmitted, g_ping_summary.received,
             g_ping_summary.loss_percent, g_ping_summary.avg_time_ms,
             g_ping_summary.duration_ms);*/

    // Clean up session
    esp_ping_delete_session(hdl);

    // Signal completion
    if (g_ping_done_sem)
    {
        xSemaphoreGive(g_ping_done_sem);
    }
}

bool ping_gateway(void)
{
    const char *TAG = "Ping";

    // Ensure station netif exists
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif)
    {
        ESP_LOGE(TAG, "No station netif found");
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get IP info");
        return false;
    }

    // Log gateway using IPSTR/IP2STR to avoid incompatible-pointer warnings
    // ESP_LOGI(TAG, "Pinging gateway: " IPSTR, IP2STR(&ip_info.gw));

    // Build ip_addr_t target_addr from ip_info.gw
    ip_addr_t target_addr;
    IP_ADDR4(&target_addr, ip4_addr1(&ip_info.gw), ip4_addr2(&ip_info.gw), ip4_addr3(&ip_info.gw), ip4_addr4(&ip_info.gw));

    // ESP_LOGI(TAG, "Pinging gateway: " IPSTR, IP2STR(&ip_info.gw));

    // Reset summary
    memset(&g_ping_summary, 0, sizeof(g_ping_summary));

    // Create (or reuse) semaphore
    if (g_ping_done_sem == NULL)
    {
        g_ping_done_sem = xSemaphoreCreateBinary();
        if (g_ping_done_sem == NULL)
        {
            ESP_LOGE(TAG, "Failed to create semaphore");
            return false;
        }
    }
    else
    {
        // Drain any previous signal
        xSemaphoreTake(g_ping_done_sem, 0);
    }

    // Configure ping session
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = 5;                 // number of probes
    config.interval_ms = 1000;        // 1s between pings
    config.timeout_ms = 1000;         // per‑ping timeout
    config.target_addr = target_addr; // gateway

    esp_ping_callbacks_t cbs = {
        .on_ping_end = on_ping_end,
        // You can add per‑reply logging if you want:
        // .on_ping_success = your_success_cb,
        // .on_ping_timeout = your_timeout_cb,
    };

    // Start session
    esp_ping_handle_t ping = NULL;
    esp_err_t err = esp_ping_new_session(&config, &cbs, &ping);
    if (err != ESP_OK || ping == NULL)
    {
        ESP_LOGE(TAG, "esp_ping_new_session failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ping_start(ping);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ping_start failed: %s", esp_err_to_name(err));
        esp_ping_delete_session(ping);
        return false;
    }

    // Wait for completion (5 pings + buffer)
    // Duration profile will be ~count * interval + timeouts; use a safe upper bound
    const TickType_t wait_ticks = pdMS_TO_TICKS(10000);
    if (xSemaphoreTake(g_ping_done_sem, wait_ticks) != pdTRUE)
    {
        ESP_LOGW(TAG, "Ping session timeout waiting for completion");
        // If still running, stop and cleanup
        esp_ping_stop(ping);
        esp_ping_delete_session(ping);
        return false;
    }

    // Success = at least one reply
    return g_ping_summary.success;
}

void formatted_uptime(char *buf, size_t bufsize)
{
    uint64_t uptime = esp_timer_get_time() / 1000000; // uptime in seconds

    int uptime_day = uptime / (60 * 60 * 24);
    int uptime_hour = (uptime / (60 * 60)) % 24;
    int uptime_minute = (uptime / 60) % 60;
    int uptime_second = uptime % 60;

    snprintf(buf, bufsize, "%dd %dh %02dm %02ds", uptime_day, uptime_hour, uptime_minute, uptime_second);
}

void formatted_daydatetime(char *buf, size_t bufsize)
{
    struct tm temp_time;
    time_t now;

    static const char *day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    // Read current time and put data in struct temp_time
    time(&now);
    localtime_r(&now, &temp_time);

    snprintf(buf, bufsize, "%s %04d/%02d/%02d-%02d:%02d:%02d", day_names[temp_time.tm_wday], temp_time.tm_year, temp_time.tm_mon + 1, temp_time.tm_mday, temp_time.tm_hour, temp_time.tm_min, temp_time.tm_sec);
}

void formatted_datetime(char *buf, size_t bufsize)
{
    struct tm temp_time;
    time_t now = 0;

    // Read local time and put data in struct temp_time
    time(&now);
    localtime_r(&now, &temp_time);

    snprintf(buf, bufsize, "%04d/%02d/%02d-%02d:%02d:%02d", temp_time.tm_year, temp_time.tm_mon + 1, temp_time.tm_mday, temp_time.tm_hour, temp_time.tm_min, temp_time.tm_sec);
}

void show_system_and_task_stats(void)
{
    const char *TAG = "sys_snapshot";

    const size_t SNAPSHOT_BUF_SIZE = 32 * 1024; // adjust if you have many tasks or long output
    char *out = malloc(SNAPSHOT_BUF_SIZE);

    const char *state_char_from_enum(eTaskState s)
    {
        switch (s)
        {
        case eRunning:
            return "R ";
        case eReady:
            return "RD";
        case eBlocked:
            return "B ";
        case eSuspended:
            return "S ";
        case eDeleted:
            return "D ";
        default:
            return "? ";
        }
    }

    if (!out)
    {
        ESP_LOGE(TAG, "No memory for snapshot buffer");
        return;
    }

    out[0] = '\0';
    size_t used = 0;

    // Header / metadata
    TickType_t ticks = xTaskGetTickCount();
    uint64_t ms = (uint64_t)ticks * portTICK_PERIOD_MS;
    esp_reset_reason_t rr = esp_reset_reason();

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "==== System snapshot ====\n Uptime: %" PRIu64 " ms. Reset reason: %d\n\n", ms, (int)rr);

    // --- Task table header ---
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "%-20s %-4s %-4s %-10s %-12s %-6s\n", "Task Name", "St", "Prio", "Stack B", "RunTicks", "CPU%");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "%-20s %-4s %-4s %-10s %-12s %-6s\n", "--------------------", "--", "----", "----------", "------------", "------");

    // Get task count and allocate TaskStatus_t array
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count == 0)
    {
        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "No tasks\n\n");
        printf("%s", out);
        free(out);
        return;
    }

    TaskStatus_t *tasks = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (!tasks)
    {
        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "Out of memory getting tasks\n\n");
        printf("%s", out);
        free(out);
        return;
    }

    // total run time counter (units depend on your run-time counter implementation)
    uint32_t total_run_time = uxTaskGetSystemState(tasks, task_count, NULL);

    // For deterministic ordering, you may sort by name or runtime; we'll sort by runtime descending
    // Simple bubble sort (task_count is usually small)
    for (UBaseType_t i = 0; i < task_count; ++i)
    {
        for (UBaseType_t j = i + 1; j < task_count; ++j)
        {
            if (tasks[j].ulRunTimeCounter > tasks[i].ulRunTimeCounter)
            {
                TaskStatus_t tmp = tasks[i];
                tasks[i] = tasks[j];
                tasks[j] = tmp;
            }
        }
    }

    for (UBaseType_t i = 0; i < task_count; ++i)
    {
        TaskStatus_t *ts = &tasks[i];

        uint32_t highwater_bytes = (uint32_t)ts->usStackHighWaterMark * (uint32_t)sizeof(StackType_t);
        float pct = 0.0f;
        if (total_run_time > 0)
        {
            pct = (100.0f * ts->ulRunTimeCounter) / (float)total_run_time;
        }

        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "%-20s %-4s %4u %10u %12u %6.2f\n", ts->pcTaskName ? ts->pcTaskName : "<no-name>",
                         state_char_from_enum(ts->eCurrentState), (unsigned)ts->uxCurrentPriority, (unsigned)highwater_bytes, (unsigned)ts->ulRunTimeCounter, pct);

        if (used + 256 >= SNAPSHOT_BUF_SIZE)
            break; // avoid overflow
    }

    vPortFree(tasks);

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "\n");

    // --- Heap table ---
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "%-20s %-12s %-12s\n", "Heap Metric", "Value (bytes)", "Notes");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "%-20s %-12s %-12s\n", "--------------------", "------------", "------------");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "%-20s %12u %-12s\n", "free_heap", (unsigned)free_heap, "");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "%-20s %12u %-12s\n", "min_ever_free", (unsigned)min_free, "");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "%-20s %12u %-12s\n", "largest_free_block", (unsigned)largest, "");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "\n");

    // --- PSRAM table (if present) ---
#if CONFIG_SPIRAM_ALLOW_NESTED_RAM_ALLOC || CONFIG_SPIRAM
    // use heap_caps for PSRAM metrics
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %-12s\n", "PSRAM Metric", "Value (bytes)");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %-12s\n", "--------------------", "------------");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %12u\n", "psram_free", (unsigned)psram_free);
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %12u\n\n", "psram_min_ever", (unsigned)psram_min);
#endif

    // --- Footer with quick fragmentation stats ---
    float frag_pct = 0.0f;
    if (free_heap > 0)
        frag_pct = 100.0f * (1.0f - ((float)largest / (float)free_heap));
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "Heap fragmentation estimate: %.1f%% (1 - largest/free)\n", frag_pct);

    // Print once
    printf("%s", out);

    free(out);
}

void set_log_levels(void)
{
    // Set log levels, in general only error are required unless debugging

    // sp_log_level_set("i2cdev", PROJECT_LOG_LEVEL);
    // esp_log_level_set("i2c.master", PROJECT_LOG_LEVEL);
    esp_log_level_set("scd4x", ESP_LOG_ERROR);
    esp_log_level_set("tca9548", ESP_LOG_ERROR);
    esp_log_level_set("aht", ESP_LOG_ERROR);
    esp_log_level_set("esp_littlefs", ESP_LOG_ERROR);
    esp_log_level_set("nvs", ESP_LOG_ERROR);
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("esp_netif_lwip", ESP_LOG_ERROR);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_ERROR);
    esp_log_level_set("event", ESP_LOG_ERROR);
    esp_log_level_set("wifi_init_default", ESP_LOG_ERROR);
    esp_log_level_set("esp_adapter", ESP_LOG_ERROR);
    esp_log_level_set("DS3231", ESP_LOG_INFO);
    esp_log_level_set("gpio", ESP_LOG_ERROR);
    esp_log_level_set("httpd", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_ws", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
    esp_log_level_set("httpd_sess", ESP_LOG_ERROR);
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    // esp_log_level_set("*", ESP_LOG_ERROR);
}

void print_html_file(const char *filepath)
{
    const char *TAG = "print";

    FILE *f = fopen(filepath, "r");

    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open %s", filepath);
        return;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), f))
    {
        // Print each line to console
        printf("%s", buf);
    }

    fclose(f);
    ESP_LOGI(TAG, "Finished printing %s", filepath);
}