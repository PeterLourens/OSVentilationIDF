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

extern const char index_ws_js_start[] asm("_binary_index_ws_js_start");
extern const char index_ws_js_end[] asm("_binary_index_ws_js_end");

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

// Serve index_ws.js
esp_err_t index_ws_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    size_t len = index_ws_js_end - index_ws_js_start - 1;
    return httpd_resp_send(req, index_ws_js_start, len);
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
            ESP_LOGI(TAG, "GetStats requested via WebSocket");
            show_system_and_task_stats();
        }

        if (strncmp((char *)ws_pkt.payload, "get_index_data", 14) == 0)
        {
            ESP_LOGI(TAG, "Index data requested via WebSocket");
            send_index_html_ws();
        }

        free(buf);
    }

    return ESP_OK;
}

/* POST handler: save bus0 config to /data/json/bus0_sensor_config.json */
esp_err_t sensorconfig_bus0_post_handler(httpd_req_t *req)
{
    size_t len = req->content_len;
    if (len == 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");

    char *buf = heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
    if (!buf)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mem");

    size_t rec = 0;
    while (rec < len)
    {
        int r = httpd_req_recv(req, buf + rec, len - rec);
        if (r <= 0)
        {
            free(buf);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
        }
        rec += (size_t)r;
    }
    buf[len] = '\0';

    cJSON *arr = build_bus_config_from_body_urlencoded(buf, len, "bus0_sensor");
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/bus0_sensor_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_sensor_config)
    if (load_sensor_config("bus0_sensor_config") != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to reload bus0 config after write");
    }

    // return the page body (original sensor_config.html)
    httpd_resp_set_type(req, "text/html");
    len = sensor_config_html_end - sensor_config_html_start;
    return httpd_resp_send(req, sensor_config_html_start, len);
}

/* POST handler: save bus1 config to /data/json/bus1_sensor_config.json */
esp_err_t sensorconfig_bus1_post_handler(httpd_req_t *req)
{
    size_t len = req->content_len;
    if (len == 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");

    char *buf = heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
    if (!buf)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mem");

    size_t rec = 0;
    while (rec < len)
    {
        int r = httpd_req_recv(req, buf + rec, len - rec);
        if (r <= 0)
        {
            free(buf);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
        }
        rec += (size_t)r;
    }
    buf[len] = '\0';

    cJSON *arr = build_bus_config_from_body_urlencoded(buf, len, "bus1_sensor");
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/bus1_sensor_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_sensor_config)
    if (load_sensor_config("bus1_sensor_config") != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to reload bus1 config after write");
    }

    // return the page body (original sensor_config.html)
    httpd_resp_set_type(req, "text/html");
    len = sensor_config_html_end - sensor_config_html_start;
    return httpd_resp_send(req, sensor_config_html_start, len);
}

/* POST handler: delete bus0 config file */
esp_err_t delete_bus0_config_post_handler(httpd_req_t *req)
{
    const char *path = "/data/json/bus0_sensor_config.json";
    remove(path); // ignore errors

    // return original page body
    httpd_resp_set_type(req, "text/html");
    size_t len = sensor_config_html_end - sensor_config_html_start;
    return httpd_resp_send(req, sensor_config_html_start, len);
}

/* POST handler: delete bus1 config file */
esp_err_t delete_bus1_config_post_handler(httpd_req_t *req)
{
    const char *path = "/data/json/bus1_sensor_config.json";
    remove(path); // ignore errors

    // return original page body
    httpd_resp_set_type(req, "text/html");
    size_t len = sensor_config_html_end - sensor_config_html_start;
    return httpd_resp_send(req, sensor_config_html_start, len);
}

// Set valve positions in NVS to 0
esp_err_t reset_valves_handler(httpd_req_t *req)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("valves", NVS_READWRITE, &nvs);

    if (err == ESP_OK)
    {
        for (int i = 0; i < 12; i++)
        {
            char key[16];
            snprintf(key, sizeof(key), "valve%d", i);
            nvs_set_i32(nvs, key, 0); // creates or overwrites
        }
        nvs_commit(nvs);
        nvs_close(nvs);
        // fall through to return page
    }
    else
    {
        ESP_LOGW(TAG, "NVS open failed for reset_valves_handler: %d", err);
        // still return the page so UI stays consistent
    }

    // Return the original valvecontrol.html page
    httpd_resp_set_type(req, "text/html");
    size_t len = valvecontrol_html_end - valvecontrol_html_start;
    return httpd_resp_send(req, valvecontrol_html_start, len);
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
    config.max_uri_handlers = 30;                 // increase from default 8 to 30
    config.stack_size = 12288;                    // increase stack size for websocket handling
    config.task_priority = tskIDLE_PRIORITY + 20; // increase with 20 for better performance
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

    httpd_uri_t index_ws_js_uri = {
        .uri = "/index_ws.js",
        .method = HTTP_GET,
        .handler = index_ws_js_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &index_ws_js_uri);

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

    httpd_uri_t reset_valves_uri = {
        .uri = "/reset_valve_pos",
        .method = HTTP_POST,
        .handler = reset_valves_handler,
    };
    httpd_register_uri_handler(http_server, &reset_valves_uri);

    httpd_uri_t sensorconfig1_post_uri = {
        .uri = "/sensorconfig1",
        .method = HTTP_POST,
        .handler = sensorconfig_bus0_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &sensorconfig1_post_uri);

    httpd_uri_t sensorconfig2_post_uri = {
        .uri = "/sensorconfig2",
        .method = HTTP_POST,
        .handler = sensorconfig_bus1_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &sensorconfig2_post_uri);

    httpd_uri_t sensorconfig1_delete_post_uri = {
        .uri = "delete_sensor_config_file1",
        .method = HTTP_POST,
        .handler = sensorconfig_bus0_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &sensorconfig1_delete_post_uri);

    httpd_uri_t sensorconfig2_delete_post_uri = {
        .uri = "/delete_sensor_config_file2",
        .method = HTTP_POST,
        .handler = sensorconfig_bus1_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &sensorconfig2_delete_post_uri);

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
        // size_t clean_len = remove_ansi_escape_codes(log_print_buffer, clean_buf, ret);
        remove_ansi_escape_codes(log_print_buffer, clean_buf, ret);

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

/* atomic write helper — writes tmp -> rename */
bool write_json_atomic_fs(const char *path, cJSON *root)
{
    if (!path || !root)
        return false;

    char tmp_path[128];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    if (sensor_cfg_mutex)
        xSemaphoreTake(sensor_cfg_mutex, portMAX_DELAY);

    FILE *f = fopen(tmp_path, "w");
    if (!f)
    {
        if (sensor_cfg_mutex)
            xSemaphoreGive(sensor_cfg_mutex);
        return false;
    }

    char *out = cJSON_PrintUnformatted(root);
    if (!out)
    {
        fclose(f);
        if (sensor_cfg_mutex)
            xSemaphoreGive(sensor_cfg_mutex);
        return false;
    }

    fwrite(out, 1, strlen(out), f);
    fclose(f);
    free(out);

    int r = rename(tmp_path, path);

    if (sensor_cfg_mutex)
        xSemaphoreGive(sensor_cfg_mutex);

    return (r == 0);
}

/* Build a JSON array for one bus from urlencoded POST body.
   field naming in the HTML: busN_sensorX_<type|valve|location|rh|co2> */
cJSON *build_bus_config_from_body_urlencoded(const char *body, size_t len, const char *bus_prefix)
{
    if (!body || !bus_prefix)
        return NULL;
    cJSON *arr = cJSON_CreateArray();
    if (!arr)
        return NULL;

    char key[64], val[256];

    for (int ch = 0; ch < CHANNELS_PER_BUS; ++ch)
    {
        cJSON *obj = cJSON_CreateObject();
        if (!obj)
        {
            cJSON_Delete(arr);
            return NULL;
        }

        /* type */
        snprintf(key, sizeof(key), "%s%d_type", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddStringToObject(obj, "type", val);
        else
            cJSON_AddStringToObject(obj, "type", "None");

        /* valve */
        snprintf(key, sizeof(key), "%s%d_valve", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddStringToObject(obj, "valve", val);
        else
            cJSON_AddStringToObject(obj, "valve", "None");

        /* location */
        snprintf(key, sizeof(key), "%s%d_location", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddStringToObject(obj, "location", val);
        else
            cJSON_AddStringToObject(obj, "location", "");

        /* RH control (On/Off) -> boolean */
        snprintf(key, sizeof(key), "%s%d_rh", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddBoolToObject(obj, "rh_enabled", (strcmp(val, "On") == 0));
        else
            cJSON_AddBoolToObject(obj, "rh_enabled", false);

        /* CO2 control (On/Off) -> boolean */
        snprintf(key, sizeof(key), "%s%d_co2", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddBoolToObject(obj, "co2_enabled", (strcmp(val, "On") == 0));
        else
            cJSON_AddBoolToObject(obj, "co2_enabled", false);

        cJSON_AddItemToArray(arr, obj);
    }

    return arr;
}