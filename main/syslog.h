#ifndef SYSLOG_H
#define SYSLOG_H

#include "esp_netif.h"
#include "sys/socket.h"

#include "globals.h"
#include "general.h"
#include "logging.h"

/* ---- Syslog destination (remote syslog-ng) ---- */
#define SYSLOG_SERVER_IP "192.168.0.5" /* change to your server */
#define SYSLOG_SERVER_PORT 515
#define SYSLOG_HOSTNAME "esp32s3"
#define SYSLOG_APPNAME "panic"

void syslog_send_line(const char *);
bool network_wait_for_ip(int, int, const char *);
void syslog_enable(void);
void syslog_disable(void);

#endif