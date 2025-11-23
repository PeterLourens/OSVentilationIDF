#include "http_functions.h"

static const char *TAG = "http server";

// Used to concact two log lines of the log output
static char ws_prefix_buf[256]; // holds prefix until message arrives
static bool ws_prefix_pending = false;

extern const char pure_min_css_start[] asm("_binary_pure_min_css_start");
extern const char pure_min_css_end[] asm("_binary_pure_min_css_end");

extern const char styles_css_start[] asm("_binary_styles_css_start");
extern const char styles_css_end[] asm("_binary_styles_css_end");

extern const char ui_js_start[] asm("_binary_ui_js_start");
extern const char ui_js_end[] asm("_binary_ui_js_end");

extern const char webserial_js_start[] asm("_binary_webserial_js_start");
extern const char webserial_js_end[] asm("_binary_webserial_js_end");

extern const char stats_js_start[] asm("_binary_stats_js_start");
extern const char stats_js_end[] asm("_binary_stats_js_end");

extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

extern const char sensor_config_html_start[] asm("_binary_sensor_config_html_start");
extern const char sensor_config_html_end[] asm("_binary_sensor_config_html_end");

extern const char settings_html_start[] asm("_binary_settings_html_start");
extern const char settings_html_end[] asm("_binary_settings_html_end");

extern const char statemachine_html_start[] asm("_binary_statemachine_html_start");
extern const char statemachine_html_end[] asm("_binary_statemachine_html_end");

extern const char valvecontrol_html_start[] asm("_binary_valvecontrol_html_start");
extern const char valvecontrol_html_end[] asm("_binary_valvecontrol_html_end");

extern const char webserial_html_start[] asm("_binary_webserial_html_start");
extern const char webserial_html_end[] asm("_binary_webserial_html_end");

extern const char stats_html_start[] asm("_binary_stats_html_start");
extern const char stats_html_end[] asm("_binary_stats_html_end");

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

// Serve stats.js
esp_err_t stats_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    size_t len = stats_js_end - stats_js_start - 1;
    return httpd_resp_send(req, stats_js_start, len);
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

// Serve sensor_config.html
esp_err_t sensor_config_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = sensor_config_html_end - sensor_config_html_start;
    return httpd_resp_send(req, sensor_config_html_start, len);
}

// Serve settings.html
esp_err_t settings_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = settings_html_end - settings_html_start;
    return httpd_resp_send(req, settings_html_start, len);
}

// Serve statemachine.html
esp_err_t statemachine_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = statemachine_html_end - statemachine_html_start;
    return httpd_resp_send(req, statemachine_html_start, len);
}

// Serve valvecontrol.html
esp_err_t valvecontrol_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = valvecontrol_html_end - valvecontrol_html_start;
    return httpd_resp_send(req, valvecontrol_html_start, len);
}

// Serve webserial.html
esp_err_t webserial_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = webserial_html_end - webserial_html_start;
    return httpd_resp_send(req, webserial_html_start, len);
}

// Serve stats.html
esp_err_t stats_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = stats_html_end - stats_html_start;
    return httpd_resp_send(req, stats_html_start, len);
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

        if (strncmp((char *)ws_pkt.payload, "GetStats", 8) == 0)
        {
            show_system_and_task_stats();
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
    config.max_uri_handlers = 16; // increase from default 8 to 16
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

    httpd_uri_t stats_js_uri = {
        .uri = "/stats.js",
        .method = HTTP_GET,
        .handler = stats_js_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &stats_js_uri);

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

    httpd_uri_t sensor_config_uri = {
        .uri = "/sensorconfig",
        .method = HTTP_GET,
        .handler = sensor_config_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &sensor_config_uri);

    httpd_uri_t settings_uri = {
        .uri = "/settings",
        .method = HTTP_GET,
        .handler = settings_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &settings_uri);

    httpd_uri_t statemachine_uri = {
        .uri = "/statemachine",
        .method = HTTP_GET,
        .handler = statemachine_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &statemachine_uri);

    httpd_uri_t valvecontrol_uri = {
        .uri = "/valvecontrol",
        .method = HTTP_GET,
        .handler = valvecontrol_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &valvecontrol_uri);

    httpd_uri_t webserial_uri = {
        .uri = "/webserial",
        .method = HTTP_GET,
        .handler = webserial_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &webserial_uri);

    httpd_uri_t stats_uri = {
        .uri = "/stats",
        .method = HTTP_GET,
        .handler = stats_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &stats_uri);

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

size_t remove_ansi_escape_codes(const char *input, char *output, size_t len)
{
    const char *ptr_in = input;
    char *ptr_out = output;

    while (ptr_in < input + len)
    {
        if (*ptr_in == '\033')
        {
            // Skip until 'm' or end
            const char *esc_end = strchr(ptr_in, 'm');
            if (esc_end)
            {
                ptr_in = esc_end + 1;
            }
            else
            {
                break; // malformed escape, stop
            }
        }
        else
        {
            *ptr_out++ = *ptr_in++;
        }
    }

    *ptr_out = '\0';           // null terminate
    return (ptr_out - output); // number of chars written
}

int websocket_log_output(const char *fmt, va_list args)
{
    static char log_print_buffer[512];
    int ret = vsnprintf(log_print_buffer, sizeof(log_print_buffer), fmt, args);

    if (ret > 0)
    {
        // Always print to UART as-is
        fwrite(log_print_buffer, 1, ret, stdout);

        // Strip ANSI codes for WebSocket
        static char clean_buf[512];
        size_t clean_len = remove_ansi_escape_codes(log_print_buffer, clean_buf, ret);

        // Detect prefix vs message
        if (!ws_prefix_pending && strstr(clean_buf, ":"))
        {
            // Likely the prefix line (ends with ":")
            strncpy(ws_prefix_buf, clean_buf, sizeof(ws_prefix_buf));
            ws_prefix_pending = true;
        }
        else if (ws_prefix_pending)
        {
            // Concatenate prefix + message
            char final_buf[768];
            int final_len = snprintf(final_buf, sizeof(final_buf), "%s %s", ws_prefix_buf, clean_buf);

            ws_async_send(final_buf, final_len);
            ws_prefix_pending = false; // reset
        }
    }
    return ret;
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