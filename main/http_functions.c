#include "http_functions.h"

static const char *TAG = "http server";

extern const char pure_min_css_start[] asm("_binary_pure_min_css_start");
extern const char pure_min_css_end[] asm("_binary_pure_min_css_end");

extern const char styles_css_start[] asm("_binary_styles_css_start");
extern const char styles_css_end[] asm("_binary_styles_css_end");

extern const char ui_js_start[] asm("_binary_ui_js_start");
extern const char ui_js_end[] asm("_binary_ui_js_end");

extern const char webserial_js_start[] asm("_binary_webserial_js_start");
extern const char webserial_js_end[] asm("_binary_webserial_js_end");

extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

extern const char webserial_html_start[] asm("_binary_webserial_html_start");
extern const char webserial_html_end[] asm("_binary_webserial_html_end");

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

// Serve ui.js
esp_err_t ui_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    size_t len = ui_js_end - ui_js_start - 1;
    return httpd_resp_send(req, ui_js_start, len);
}

// Serve webserial.js
esp_err_t webserial_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    size_t len = webserial_js_end - webserial_js_start - 1;
    return httpd_resp_send(req, webserial_js_start, len);
}

// Serve favicon.ico
esp_err_t favicon_ico_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/x-icon");
    size_t len = favicon_ico_end - favicon_ico_start;
    return httpd_resp_send(req, (const char *)favicon_ico_start, len);
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
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;

    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        http_server = req->handle;
        return ESP_OK;
    }

    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Set max_len = 0 to get the frame len
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;

        // Set max_len = ws_pkt.len to get the frame payload
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }

        if (strncmp((char *)ws_pkt.payload, "Start", 5) == 0)
        {
            // Redirect log output to the custom function
            esp_log_set_vprintf(websocket_log_output);
            ESP_LOGI(TAG, "Redirecting console to websocket");
        }

        if (strncmp((char *)ws_pkt.payload, "Stop", 4) == 0)
        {
            // Redirect log output to the custom function
            ESP_LOGI(TAG, "Stopped redirecting console to websocket");
            esp_log_set_vprintf(uart_log_output);
        }

        free(buf);
    }

    return ESP_OK;
}

void ws_async_send(char *buf, size_t len)
{
    if (http_server == NULL)
        return;

    httpd_ws_frame_t ws_pkt;

    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    ws_pkt.payload = (uint8_t *)buf;
    ws_pkt.len = len;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Obtain file descriptors from httpd server
    static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];

    esp_err_t ret = httpd_get_client_list(http_server, &fds, client_fds);

    if (ret != ESP_OK)
    {
        return;
    }

    for (int i = 0; i < fds; i++)
    {
        int client_info = httpd_ws_get_fd_info(http_server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET)
        {
            httpd_ws_send_frame_async(http_server, client_fds[i], &ws_pkt);
        }
    }
}

// Start webserver
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&http_server, &config));

    httpd_uri_t pure_min_css_uri = {
        .uri = "/pure_min.css",
        .method = HTTP_GET,
        .handler = pure_min_css_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &pure_min_css_uri);

    httpd_uri_t styles_css_uri = {
        .uri = "/styles.css",
        .method = HTTP_GET,
        .handler = styles_css_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &styles_css_uri);

    httpd_uri_t ui_js_uri = {
        .uri = "/ui.js",
        .method = HTTP_GET,
        .handler = ui_js_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &ui_js_uri);

    httpd_uri_t webserial_js_uri = {
        .uri = "/webserial.js",
        .method = HTTP_GET,
        .handler = webserial_js_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &webserial_js_uri);

    httpd_uri_t favicon_ico_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_ico_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &favicon_ico_uri);

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &index_uri);

    httpd_uri_t webserial_uri = {
        .uri = "/webserial",
        .method = HTTP_GET,
        .handler = webserial_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &webserial_uri);

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true};
    httpd_register_uri_handler(http_server, &ws_uri);

    return http_server;
}

esp_err_t http_server_stop(void)
{
    int ret;

    ESP_LOGI(TAG, "Stopping HTTP server");

    if (http_server)
    {
        ret = httpd_stop(http_server);
    }
    else
    {
        ret = ESP_ERR_INVALID_ARG;
    }
    return ret;
}

size_t remove_ansi_escape_codes(char *input, char *output, size_t len)
{
    char *ptr_out = output;
    char *ptr_in = input;

    while (ptr_in <= (input + len - 1))
    {
        if (*ptr_in == '\033')
        {
            // Search for 'm';
            ptr_in = strchr(ptr_in, 'm') + 1;
        }
        else
        {
            *ptr_out++ = *ptr_in++;
        }
    }
    return ((ptr_out - 1) - output);
}

int websocket_log_output(const char *szFormat, va_list args)
{
    static char log_print_buffer[512];
    static char buffer[512];

    int ret = vsnprintf(log_print_buffer, sizeof(log_print_buffer), szFormat, args);

    if (ret >= 0)
    {
        // Also copy console to UART
        printf("%.*s", ret, log_print_buffer);
        // esp-idf use ANSI escape codes to colour text on the console
        ret = remove_ansi_escape_codes(log_print_buffer, buffer, ret);
        ws_async_send(buffer, ret);
    }
    return (0);
}

int uart_log_output(const char *szFormat, va_list args)
{
    static char log_print_buffer[512];
    int ret = vsnprintf(log_print_buffer, sizeof(log_print_buffer), szFormat, args);
    if (ret >= 0)
    {
        // Also copy console to UART
        printf("%.*s", ret, log_print_buffer);
    }
    return (0);
}