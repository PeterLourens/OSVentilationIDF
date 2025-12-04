#ifndef HTTP_SEND_WS_DATA_H
#define HTTP_SEND_WS_DATA_H

#include "globals.h"
#include "general.h"
#include "http_functions.h"

// Function definitions
void send_sensor_config_json_ws(void);
void send_config_json_ws(void);
void send_index_html_ws(void);

#endif
