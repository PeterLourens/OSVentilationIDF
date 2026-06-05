/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file fancontrol.h
 * @brief Public fan control interface for HTTP and MQTT-driven fans.
 */

#ifndef FANCONTROL_H
#define FANCONTROL_H

#include <driver/i2c_master.h>
#include <esp_http_client.h>
#include <esp_err.h>
#include <string.h>
#include <stdio.h>

#include "globals.h"
#include "general.h"
#include "logging.h"

/**
 * @brief Set the target fan speed using the configured fan control method.
 *
 * The configured mode may be HTTP API or MQTT publish; the corresponding URL or
 * MQTT topic is read from the fan configuration.
 *
 * @param fanspeed Desired fanspeed: 0=error/unset, 1=low, 2=medium, 3=high.
 */
void set_fanspeed(int fanspeed);

#endif