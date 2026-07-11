/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file stats_worker.c
 * @brief Background worker for stats requests and WebSocket statistics publishing.
 */

#include "stats_worker.h"

static const char *TAG = "stats_worker";
#define STATS_WORKER_STACK 4096
#define STATS_WORKER_PRIO 5

static QueueHandle_t stats_req_q = NULL;

static void worker_task(void *pv)
{
    // char cmd[32];
    while (1)
    {
        // if (xQueueReceive(stats_req_q, &cmd, portMAX_DELAY) == pdTRUE)
        //{
        //   if (strcmp(cmd, "stats") == 0)
        //{
        //   cJSON *root = sys_stats_build_json();
        // if (root)
        //   ws_broadcaster_send(root);
        //}
        // else if (strcmp(cmd, "statemachine") == 0)
        //{
        //  Reuse your existing send_statemachine_config_json_ws logic but return cJSON instead.
        //    cJSON *root = build_statemachine_config_cjson(); // see note below
        //    if (root)
        //        ws_broadcaster_send(root);
        //}
        // else if (strcmp(cmd, "index") == 0)
        //{
        //  cJSON *root = build_index_cjson(); // see note below
        // if (root)
        //    ws_broadcaster_send(root);
        //}
    }
}

void stats_worker_init(void)
{
    if (stats_req_q)
        return;
    stats_req_q = xQueueCreate(8, 32);
    if (!stats_req_q)
    {
        ESP_LOGE(TAG, "Failed to create stats request queue");
        return;
    }
    xTaskCreate(worker_task, "stats_worker", STATS_WORKER_STACK / sizeof(StackType_t), NULL, STATS_WORKER_PRIO, NULL);
}

bool stats_worker_request(const char *cmd)
{
    if (!stats_req_q)
        return false;
    char buf[32];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return xQueueSend(stats_req_q, &buf, pdMS_TO_TICKS(10)) == pdTRUE;
}