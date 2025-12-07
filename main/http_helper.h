#ifndef HTTP_HELPER_H
#define HTTP_HELPER_H

#include "globals.h"
#include "general.h"

// Function declarations
size_t remove_ansi_escape_codes(const char *, char *, size_t);
int websocket_log_output(const char *, va_list);
int uart_log_output(const char *, va_list);
bool write_json_atomic_fs(const char *, cJSON *);

#endif