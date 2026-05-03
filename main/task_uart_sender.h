/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
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

#include "globals.h"
#include "logging.h"

// Configuration
#ifndef SENDER_UART_NUM
#define SENDER_UART_NUM UART_NUM_0
#endif

#ifndef SENDER_UART_BAUD
#define SENDER_UART_BAUD 115200
#endif

#ifndef SENDER_UART_RX_BUF_SIZE
#define SENDER_UART_RX_BUF_SIZE 256 /* must be > UART HW FIFO (128 on many chips) */
#endif

#ifndef SENDER_TX_BUF_SIZE
#define SENDER_TX_BUF_SIZE 2048
#endif

/* Queue settings */
#define UART_SENDER_QUEUE_LEN 32 /* number of pending messages */
#define UART_SENDER_MAX_LINE 512 /* max bytes per line (including CRLF) */

/* Static task storage for the user-visible task */
#define UART_SENDER_STACK_WORDS 4096

// function declarations
void uart_sender_sendf(const char *fmt, ...);
void start_task_uart_sender(void);
void task_uart_sender_code(void *pvParameters);

#endif