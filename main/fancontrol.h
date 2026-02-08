/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
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

/* Control fans via configured mechanism. Pass fanspeed as an integer:
 * 0 = error, 1 = low, 2 = medium, 3 = high
 */
void set_fanspeed(int fanspeed);

#endif /* FANCONTROL_H */