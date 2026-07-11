/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_uart_sender.h
 * @brief UART sender task public declarations and logging helpers.
 */

#ifndef TASK_UART_SENDER_H
#define TASK_UART_SENDER_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <stdlib.h>
#include <esp_attr.h>

#include "globals.h"
#include "logging.h"

// Configuration
#ifndef SENDER_UART_NUM
#define SENDER_UART_NUM UART_NUM_1
#endif

#ifndef SENDER_UART_BAUD
#define SENDER_UART_BAUD 115200
#endif

#ifndef SENDER_UART_RX_BUF_SIZE
#define SENDER_UART_RX_BUF_SIZE 256 // must be > UART HW FIFO (128 on many chips)
#endif

#ifndef SENDER_TX_BUF_SIZE
#define SENDER_TX_BUF_SIZE 2048 // must be >= max line length to avoid truncation; increased for safety
#endif

// Queue settings
#define UART_SENDER_QUEUE_LEN 128
#define UART_SENDER_MAX_LINE 512 // max bytes per line (including CRLF)

// Static task storage for the user-visible task
#ifndef UART_SENDER_STACK_WORDS
#define UART_SENDER_STACK_WORDS 3072
#endif

// Fixed buffer pool configuration
#ifndef UART_POOL_COUNT
#define UART_POOL_COUNT 64
#endif
#ifndef UART_POOL_BUF_SIZE
#define UART_POOL_BUF_SIZE 512
#endif

// function declarations
size_t uart_sender_get_dropped_count(void);
size_t uart_sender_get_pool_exhaust_count(void);
int uart_log_vprintf(const char *fmt, va_list args);
void uart_sender_sendf(const char *fmt, ...);
void start_task_uart_sender(void);
void stop_task_uart_sender(void);
void task_uart_sender_code(void *pvParameters);

#endif