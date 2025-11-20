#ifndef HTTP_FUNCTIONS_H
#define HTTP_FUNCTIONS_H

#include "globals.h"
#include "general.h"

// Function declarations
int uart_log_output(const char *, va_list);
int dual_log_output(const char *, va_list);
esp_err_t pure_min_css_get_handler(httpd_req_t *);
esp_err_t styles_css_get_handler(httpd_req_t *);
esp_err_t index_get_handler(httpd_req_t *);
esp_err_t webserial_get_handler(httpd_req_t *);
esp_err_t websocket_handler(httpd_req_t *);
httpd_handle_t start_webserver(void);

#endif