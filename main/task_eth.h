/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef TASK_ETH_H
#define TASK_ETH_H

#include <esp_netif.h>
#include <esp_eth.h>
#include <esp_wifi.h>
#include <esp_mac.h>

#include "eth_functions.h"
#include "logging.h"

// Function declaraions
void start_task_eth(void);
void task_eth_set_wifi_forced(bool forced);

#endif