#include "sys_stats.h"

static const char *TAG = "Sys stats";

// -- -Helpers-- -

// Map task state enum to short string
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

// Map reset reason to human‑friendly string
const char *reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return "Power‑on";
    case ESP_RST_EXT:
        return "External reset (reset pin)";
    case ESP_RST_SW:
        return "Software reset (esp_restart)";
    case ESP_RST_PANIC:
        return "Exception/panic";
    case ESP_RST_INT_WDT:
        return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "Task watchdog";
    case ESP_RST_WDT:
        return "Other watchdog";
    case ESP_RST_DEEPSLEEP:
        return "Wake from deep sleep";
    case ESP_RST_BROWNOUT:
        return "Brownout";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "Unknown";
    }
}

void show_system_and_task_stats(void)
{
    const size_t SNAPSHOT_BUF_SIZE = 32 * 1024;
    char *out = malloc(SNAPSHOT_BUF_SIZE);
    if (!out)
        return;
    size_t used = 0;

    // Header
    // TickType_t ticks = xTaskGetTickCount();
    // uint64_t ms = (uint64_t)ticks * portTICK_PERIOD_MS;

    char uptime_str[64] = {};
    formatted_uptime(uptime_str, sizeof(uptime_str));
    ESP_LOGI(TAG, "System uptime: %s", uptime_str);

    char time_str[64] = {};
    formatted_daydatetime(time_str, sizeof(time_str));
    ESP_LOGI(TAG, "System time: %s", time_str);

    esp_reset_reason_t rr = esp_reset_reason();

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "==== System snapshot ====\n\n");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "System time: %s\nUptime: %s \nReset reason: %s\n\n", time_str, uptime_str, reset_reason_str(rr));

    // Task runtime table
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count == 0)
    {
        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "No tasks\n\n");
        goto send_out;
    }

    TaskStatus_t *tasks = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (!tasks)
    {
        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used, "Out of memory getting tasks\n\n");
        goto send_out;
    }

    uint32_t total_run_time = uxTaskGetSystemState(tasks, task_count, NULL);

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %-4s %-4s %-12s %-12s %-6s\n",
                     "Task Name", "St", "Prio", "HighWater", "RunTicks", "CPU%");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %-4s %-4s %-12s %-12s %-6s\n",
                     "--------------------", "--", "----", "------------", "------------", "------");

    for (UBaseType_t i = 0; i < task_count; i++)
    {
        TaskStatus_t *ts = &tasks[i];
        uint32_t highwater_bytes = ts->usStackHighWaterMark * sizeof(StackType_t);
        float pct = (total_run_time > 0) ? (100.0f * ts->ulRunTimeCounter) / (float)total_run_time : 0.0f;

        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                         "%-20s %-4s %4u %12u %12u %6.2f\n",
                         ts->pcTaskName ? ts->pcTaskName : "<no-name>",
                         state_char_from_enum(ts->eCurrentState),
                         (unsigned)ts->uxCurrentPriority,
                         (unsigned)highwater_bytes,
                         (unsigned)ts->ulRunTimeCounter,
                         pct);
    }

    // High‑watermark table
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "\nTask stack usage (high‑watermarks):\n");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %-12s\n", "Task Name", "HighWater (bytes)");
    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "%-20s %-12s\n", "--------------------", "--------------");

    for (UBaseType_t i = 0; i < task_count; i++)
    {
        TaskStatus_t *ts = &tasks[i];
        uint32_t highwater_bytes = ts->usStackHighWaterMark * sizeof(StackType_t);
        const char *note = (highwater_bytes < 256) ? "LOW!" : "";
        used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                         "%-20s %12u %s\n",
                         ts->pcTaskName ? ts->pcTaskName : "<no-name>",
                         (unsigned)highwater_bytes,
                         note);
    }

    vPortFree(tasks);

    // Heap metrics
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    float frag_pct = free_heap ? 100.0f * (1.0f - ((float)largest / (float)free_heap)) : 0.0f;

    used += snprintf(out + used, SNAPSHOT_BUF_SIZE - used,
                     "\nHeap metrics:\n"
                     "free_heap: %u\nmin_ever_free: %u\nlargest_free_block: %u\n"
                     "Heap fragmentation estimate: %.1f%%\n",
                     (unsigned)free_heap, (unsigned)min_free, (unsigned)largest, frag_pct);

send_out:
    // Print to UART
    printf("%s", out);

    // Send to WebSocket clients
    ws_async_send("STATS:\n", strlen("STATS:\n"));
    ws_async_send(out, used);

    free(out);
}
