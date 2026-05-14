/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "ws_broadcaster.h"

static const char *TAG = "ws_broadcaster";
#define BROADCAST_QUEUE_LEN 8
#define BROADCAST_TASK_STACK 4096

typedef struct
{
    char *buf;
    size_t len;
} broadcast_item_t;

static QueueHandle_t broadcast_queue = NULL;

static void broadcaster_task(void *pv)
{
    broadcast_item_t item;
    while (1)
    {
        if (xQueueReceive(broadcast_queue, &item, portMAX_DELAY) == pdTRUE)
        {
            if (item.buf && item.len)
            {
                // low-level async send provided by your project
                ws_async_send(item.buf, item.len);
                free(item.buf);
            }
        }
    }
}

void ws_broadcaster_init(void)
{
    if (broadcast_queue)
        return;
    broadcast_queue = xQueueCreate(BROADCAST_QUEUE_LEN, sizeof(broadcast_item_t));
    if (!broadcast_queue)
    {
        ESP_LOGE(TAG, "Failed to create broadcast queue");
        return;
    }
    xTaskCreate(broadcaster_task, "ws_broadcaster", BROADCAST_TASK_STACK / sizeof(StackType_t), NULL, tskIDLE_PRIORITY + 2, NULL);
}

void ws_broadcaster_send(cJSON *obj)
{
    if (!obj)
        return;
    char *out = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!out)
    {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted returned NULL");
        return;
    }

    broadcast_item_t item = {0};
    item.len = strlen(out);
    item.buf = malloc(item.len + 1);
    if (!item.buf)
    {
        ESP_LOGW(TAG, "Out of memory for broadcast buffer, dropping message");
        free(out);
        return;
    }
    memcpy(item.buf, out, item.len + 1);
    free(out);

    if (xQueueSend(broadcast_queue, &item, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Broadcast queue full, dropping message");
        free(item.buf);
    }
}

void ws_broadcaster_send_raw(const char *buf, size_t len)
{
    if (!buf || len == 0)
        return;
    broadcast_item_t item = {0};
    item.len = len;
    item.buf = malloc(len + 1);
    if (!item.buf)
    {
        ESP_LOGW(TAG, "Out of memory for raw broadcast");
        return;
    }
    memcpy(item.buf, buf, len);
    item.buf[len] = '\0';
    if (xQueueSend(broadcast_queue, &item, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Broadcast queue full, dropping raw message");
        free(item.buf);
    }
}
