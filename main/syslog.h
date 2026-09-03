/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file syslog.h
 * @brief Public syslog helper declarations and queue interfaces.
 */

#ifndef SYSLOG_H
#define SYSLOG_H

#include <esp_netif.h>
#include <sys/socket.h>
#include <esp_heap_caps.h>

#include "general.h"
#include "logging.h"
#include "network_functions.h"

// Queue / worker configuration
#define SYSLOG_QUEUE_LENGTH 64
#define SYSLOG_MAX_LINE_LEN 512
#define SYSLOG_WORKER_STACK 8192
#define SYSLOG_WORKER_PRIO 5
#define SYSLOG_SEND_RETRIES 3
#define SYSLOG_RETRY_DELAY_MS 500

extern volatile bool g_request_syslog_update;

void syslog_send_line(const char *);
bool network_wait_for_ip(int, int, const char *);
void syslog_enable(void);
void syslog_disable(void);
void syslog_queue_init(void);
void syslog_queue_deinit(void);
void syslog_refresh_task_state(void);
void syslog_enqueue_line(const char *line);

#endif