/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef SYSLOG_H
#define SYSLOG_H

#include <esp_netif.h>
#include <sys/socket.h>

#include "general.h"
#include "logging.h"

void syslog_send_line(const char *);
bool network_wait_for_ip(int, int, const char *);
void syslog_enable(void);
void syslog_disable(void);

#endif