#ifndef HTTP_FUNCTIONS_H
#define HTTP_FUNCTIONS_H

#include "globals.h"
#include "general.h"

// Function declarations
esp_err_t http_server_init(void);
esp_err_t http_server_stop(void);
void ws_async_send(char *, size_t);

#endif