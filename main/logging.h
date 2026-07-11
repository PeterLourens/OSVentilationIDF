/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file logging.h
 * @brief Logging macros that forward formatted logs to syslog when enabled.
 */

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <stdio.h>

#include "syslog.h"

/**
 * @brief Global flag enabling syslog forwarding of logs.
 */
extern bool syslog_enabled;

#define LOG_FORMAT_AND_SEND(tag, level, prefix, fmt, ...)                              \
    do                                                                                 \
    {                                                                                  \
        /* Always log locally */                                                       \
        ESP_LOG_LEVEL(level, tag, fmt, ##__VA_ARGS__);                                 \
                                                                                       \
        /* Respect ESP-IDF log level for syslog too */                                 \
        if (level > esp_log_level_get(tag))                                            \
            break;                                                                     \
                                                                                       \
        if (!syslog_enabled)                                                           \
            break;                                                                     \
                                                                                       \
        /* First pass: compute required size */                                        \
        int _len = snprintf(NULL, 0, fmt, ##__VA_ARGS__);                              \
        if (_len < 0)                                                                  \
            break; /* formatting error */                                              \
                                                                                       \
        /* Allocate exact buffer */                                                    \
        char *_msg = heap_caps_malloc(_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  \
        if (!_msg)                                                                     \
            break;                                                                     \
                                                                                       \
        /* Second pass: write formatted message */                                     \
        snprintf(_msg, _len + 1, fmt, ##__VA_ARGS__);                                  \
                                                                                       \
        /* Prefix for syslog */                                                        \
        int _plen = snprintf(NULL, 0, "[%s] %s", prefix, _msg);                        \
        if (_plen < 0)                                                                 \
        {                                                                              \
            heap_caps_free(_msg);                                                      \
            break;                                                                     \
        }                                                                              \
                                                                                       \
        char *_sys = heap_caps_malloc(_plen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); \
        if (!_sys)                                                                     \
        {                                                                              \
            heap_caps_free(_msg);                                                      \
            break;                                                                     \
        }                                                                              \
                                                                                       \
        snprintf(_sys, _plen + 1, "[%s] %s", prefix, _msg);                            \
                                                                                       \
        syslog_send_line(_sys);                                                        \
                                                                                       \
        heap_caps_free(_msg);                                                          \
        heap_caps_free(_sys);                                                          \
    } while (0)

#define LOGE(tag, fmt, ...) LOG_FORMAT_AND_SEND(tag, ESP_LOG_ERROR, "ERROR", fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) LOG_FORMAT_AND_SEND(tag, ESP_LOG_WARN, "WARN", fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) LOG_FORMAT_AND_SEND(tag, ESP_LOG_INFO, "INFO", fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) LOG_FORMAT_AND_SEND(tag, ESP_LOG_DEBUG, "DEBUG", fmt, ##__VA_ARGS__)
#define LOGV(tag, fmt, ...) LOG_FORMAT_AND_SEND(tag, ESP_LOG_VERBOSE, "VERBOSE", fmt, ##__VA_ARGS__)
