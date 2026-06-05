/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file general.h
 * @brief Shared public declarations for general firmware utilities.
 *
 * This header exposes initialization helpers, file I/O utilities, and
 * runtime formatting helpers used by multiple firmware modules.
 */

#ifndef GENERAL_H
#define GENERAL_H

#include <esp_littlefs.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <ping/ping_sock.h>
#include <sys/socket.h>
#include <esp_log_level.h>

#include "i2c_ds3231.h"
#include "http_functions.h"
#include "statemachine_states.h"
#include "eth_functions.h"

/**
 * @brief General utility declarations, ordered to match main/general.c.
 */
const char *state_nr_to_str(state_nr_t state);
const char *fan_speed_to_str(fan_speed_t f);
esp_err_t init_nvs(void);
esp_err_t init_littlefs(void);
esp_err_t read_file(const char *, char *, size_t);
esp_err_t create_mutexes(void);
void init_timezone(void);
void formatted_uptime(char *, size_t);
void formatted_daydatetime(char *, size_t);
void formatted_datetime(char *, size_t);
void formatted_day(char *, size_t);
void set_log_levels(void);
void print_html_file(const char *);

#endif