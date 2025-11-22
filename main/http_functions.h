#ifndef HTTP_FUNCTIONS_H
#define HTTP_FUNCTIONS_H

#include "globals.h"
#include "general.h"

#define LOG_BUF_SIZE 256

// Function declarations
esp_err_t pure_min_css_get_handler(httpd_req_t *);
esp_err_t styles_css_get_handler(httpd_req_t *);
esp_err_t index_get_handler(httpd_req_t *);
esp_err_t webserial_get_handler(httpd_req_t *);
esp_err_t websocket_handler(httpd_req_t *);
void ws_async_send(char *, size_t);
httpd_handle_t start_webserver(void);
esp_err_t http_server_stop(void);
size_t remove_ansi_escape_codes(const char *, char *, size_t);
int websocket_log_output(const char *, va_list);
int uart_log_output(const char *, va_list);

#endif