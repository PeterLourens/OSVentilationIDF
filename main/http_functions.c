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
/*int uart_log_output(const char *fmt, va_list args)
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
}*/

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
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;

    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        hd = req->handle;
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

/*esp_err_t websocket_handler(httpd_req_t *req)
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
}*/

void ws_async_send(char *buf, size_t len)
{
    if (hd == NULL)
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
            httpd_ws_send_frame_async(hd, client_fds[i], &ws_pkt);
        }
    }
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