/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef MAIN_H
#define MAIN_H

// #include "globals.h"
#include "general.h"
#include "config_files.h"
#include "i2c_functions.h"
#include "task_i2c_sensors.h"
#include "task_i2c_ds3231.h"
#include "i2c_ds3231.h"
#include "task_i2c_lcd.h"
#include "i2c_lcd.h"
#include "task_rgb_leds.h"
#include "valvecontrol.h"
#include "http_functions.h"
#include "statemachine.h"
#include "task_statemachine.h"
#include "syslog.h"
#include "task_valvecontrol.h"
#include "task_influxdb.h"
#include "task_mqtt.h"
#include "task_pushbutton.h"
#include "sys_stats.h"
#include "eth_functions.h"
#include "network_manager.h"

#define PROJECT_LOG_LEVEL ESP_LOG_INFO

#endif