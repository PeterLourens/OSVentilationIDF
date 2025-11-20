#include "http_functions.h"

static const char *TAG = "http server";

extern const char pure_min_css_start[] asm("_binary_pure_min_css_start");
extern const char pure_min_css_end[] asm("_binary_pure_min_css_end");

extern const char styles_css_start[] asm("_binary_styles_css_start");
extern const char styles_css_end[] asm("_binary_styles_css_end");

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

extern const char webserial_html_start[] asm("_binary_webserial_html_start");
extern const char webserial_html_end[] asm("_binary_webserial_html_end");

// Functions
// UART-only log output
int uart_log_output(const char *fmt, va_list args)
{
    return vprintf(fmt, args);
}

// UART + WebSocket log output
int dual_log_output(const char *fmt, va_list args)
{
    // --- UART output ---
    int len = vprintf(fmt, args);

    // --- WebSocket output ---
    char buf[256];
    va_list args_copy;
    va_copy(args_copy, args);
    int wslen = vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    if (wslen > 0)
    {
        httpd_ws_frame_t ws_pkt = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)buf,
            .len = strlen(buf),
        };

        for (int fd = 0; fd < CONFIG_LWIP_MAX_SOCKETS; fd++)
        {
            if (httpd_ws_get_fd_info(hd, fd) == HTTPD_WS_CLIENT_WEBSOCKET)
            {
                httpd_ws_send_frame_async(hd, fd, &ws_pkt);
            }
        }
    }

    return len;
}

// Serve pure_min.css
esp_err_t pure_min_css_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    size_t len = pure_min_css_end - pure_min_css_start;
    return httpd_resp_send(req, pure_min_css_start, len);
}

// Serve styles.css
esp_err_t styles_css_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    size_t len = styles_css_end - styles_css_start;
    return httpd_resp_send(req, styles_css_start, len);
}

// Serve index.html
esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = index_html_end - index_html_start;
    return httpd_resp_send(req, index_html_start, len);
}

// Serve webserial.html
esp_err_t webserial_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = webserial_html_end - webserial_html_start;
    return httpd_resp_send(req, webserial_html_start, len);
}

// WebSocket handler
esp_err_t websocket_handler(httpd_req_t *req)
{
    httpd_ws_frame_t ws_pkt = {0};
    uint8_t *buf = NULL;

    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, new connection opened");
        return ESP_OK;
    }

    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
        return ret;

    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (!buf)
            return ESP_ERR_NO_MEM;

        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            free(buf);
            return ret;
        }

        buf[ws_pkt.len] = '\0';

        if (strcmp((char *)buf, "Start") == 0)
        {
            // Switch logging first
            esp_log_set_vprintf(dual_log_output);

            // Confirmation log (goes to UART + WebSocket)
            ESP_LOGI(TAG, "Logs now go to UART + WebSocket");

            // Immediate acknowledgment frame
            httpd_ws_frame_t ack = {
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)"Logging enabled (UART + WebSocket)",
                .len = strlen("Logging enabled (UART + WebSocket)"),
            };
            httpd_ws_send_frame(req, &ack);
        }
        else if (strcmp((char *)buf, "Stop") == 0)
        {
            // Switch logging back to UART only
            esp_log_set_vprintf(uart_log_output);

            // Confirmation log (UART only now)
            ESP_LOGI(TAG, "Logs only to UART");

            // Immediate acknowledgment frame
            httpd_ws_frame_t ack = {
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)"Logging disabled (UART only)",
                .len = strlen("Logging disabled (UART only)"),
            };
            httpd_ws_send_frame(req, &ack);
        }

        free(buf);
    }
    return ESP_OK;
}

// Start webserver
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&hd, &config));

    httpd_uri_t pure_min_css_uri = {
        .uri = "/pure_min.css",
        .method = HTTP_GET,
        .handler = pure_min_css_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(hd, &pure_min_css_uri);

    httpd_uri_t styles_css_uri = {
        .uri = "/styles.css",
        .method = HTTP_GET,
        .handler = styles_css_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(hd, &styles_css_uri);

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(hd, &index_uri);

    httpd_uri_t webserial_uri = {
        .uri = "/webserial",
        .method = HTTP_GET,
        .handler = webserial_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(hd, &webserial_uri);

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true};
    httpd_register_uri_handler(hd, &ws_uri);

    return hd;
}