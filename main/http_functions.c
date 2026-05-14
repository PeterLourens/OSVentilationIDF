/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "http_functions.h"

static const char *TAG = "HTTP_server";

extern const char pure_min_css_start[] asm("_binary_pure_min_css_start");
extern const char pure_min_css_end[] asm("_binary_pure_min_css_end");

extern const char styles_css_start[] asm("_binary_styles_css_start");
extern const char styles_css_end[] asm("_binary_styles_css_end");

extern const char ui_js_start[] asm("_binary_ui_js_start");
extern const char ui_js_end[] asm("_binary_ui_js_end");

extern const char send_json_ws_js_start[] asm("_binary_send_json_ws_js_start");
extern const char send_json_ws_js_end[] asm("_binary_send_json_ws_js_end");

extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

extern const char sensor_config_html_start[] asm("_binary_sensor_config_html_start");
extern const char sensor_config_html_end[] asm("_binary_sensor_config_html_end");

extern const char config_html_start[] asm("_binary_config_html_start");
extern const char config_html_end[] asm("_binary_config_html_end");

extern const char statemachine_html_start[] asm("_binary_statemachine_html_start");
extern const char statemachine_html_end[] asm("_binary_statemachine_html_end");

extern const char valvecontrol_html_start[] asm("_binary_valvecontrol_html_start");
extern const char valvecontrol_html_end[] asm("_binary_valvecontrol_html_end");

extern const char webserial_html_start[] asm("_binary_webserial_html_start");
extern const char webserial_html_end[] asm("_binary_webserial_html_end");

extern const char stats_html_start[] asm("_binary_stats_html_start");
extern const char stats_html_end[] asm("_binary_stats_html_end");

extern const char reboot_html_start[] asm("_binary_reboot_html_start");
extern const char reboot_html_end[] asm("_binary_reboot_html_end");

// Serve pure_min.css
static esp_err_t pure_min_css_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    size_t len = pure_min_css_end - pure_min_css_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, pure_min_css_start, len);
}

// Serve styles.css
static esp_err_t styles_css_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    size_t len = styles_css_end - styles_css_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, styles_css_start, len);
}

// Serve ui.js
static esp_err_t ui_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    size_t len = ui_js_end - ui_js_start - 1;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, ui_js_start, len);
}

// Serve send_json_ws.js
static esp_err_t send_json_ws_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    size_t len = send_json_ws_js_end - send_json_ws_js_start - 1;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, send_json_ws_js_start, len);
}

// Serve favicon.ico
static esp_err_t favicon_ico_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/x-icon");
    size_t len = favicon_ico_end - favicon_ico_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, (const char *)favicon_ico_start, len);
}

// Serve index.html
static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = index_html_end - index_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, index_html_start, len);
}

// Serve sensor_config.html
static esp_err_t sensor_config_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = sensor_config_html_end - sensor_config_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, sensor_config_html_start, len);
}

// Serve config.html
static esp_err_t config_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = config_html_end - config_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, config_html_start, len);
}

// Serve statemachine.html
static esp_err_t statemachine_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = statemachine_html_end - statemachine_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, statemachine_html_start, len);
}

// Serve valvecontrol.html
static esp_err_t valvecontrol_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = valvecontrol_html_end - valvecontrol_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, valvecontrol_html_start, len);
}

// Serve webserial.html
static esp_err_t webserial_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = webserial_html_end - webserial_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, webserial_html_start, len);
}

// Serve stats.html
static esp_err_t stats_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = stats_html_end - stats_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, stats_html_start, len);
}

// WebSocket handler
static esp_err_t websocket_handler(httpd_req_t *req)
{
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;

    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    if (req->method == HTTP_GET)
    {
        LOGD(TAG, "Handshake done, the new connection was opened");
        http_server = req->handle;
        net_mgr_notify_eth_activity(); // count handshake as activity
        return ESP_OK;
    }

    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Set max_len = 0 to get the frame len
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    if (ret != ESP_OK)
    {
        LOGW(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;

        // Set max_len = ws_pkt.len to get the frame payload
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            LOGW(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }

        if (strcmp((char *)ws_pkt.payload, "Start") == 0)
        {
            esp_log_set_vprintf(websocket_log_output);
            LOGD(TAG, "Redirecting console to websocket");
        }

        if (strcmp((char *)ws_pkt.payload, "Stop") == 0)
        {
            LOGD(TAG, "Stopped redirecting console to websocket");
            esp_log_set_vprintf(uart_log_output);
        }

        if (strcmp((char *)ws_pkt.payload, "stats") == 0)
        {
            LOGD(TAG, "Stats requested via WebSocket");
            show_system_and_task_stats();
        }

        if (strcmp((char *)ws_pkt.payload, "index") == 0)
        {
            LOGD(TAG, "Index data requested via WebSocket");
            send_index_html_ws();
        }

        if (strcmp((char *)ws_pkt.payload, "sensor_config") == 0)
        {
            LOGD(TAG, "Sensor config requested via WebSocket");
            send_sensor_config_json_ws();
        }

        if (strcmp((char *)ws_pkt.payload, "config") == 0)
        {
            LOGD(TAG, "Config requested via WebSocket");
            send_config_json_ws();
        }
        if (strcmp((char *)ws_pkt.payload, "statemachine") == 0)
        {
            LOGD(TAG, "Statemachine config requested via WebSocket");
            send_statemachine_config_json_ws();
        }

        free(buf);
    }

    return ESP_OK;
}

// POST handler: save bus0 config to /data/json/bus0_sensor_config.json
static esp_err_t sensorconfig_bus0_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_bus_config_from_body_urlencoded(buf, len, "b0_s");
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
        LOGE(TAG, "Failed to reload bus0 config after write");
    }

    // After reconfig of sensors the sensors should re-init
    reload_and_init_sensors();

    // return the page body (original sensor_config.html)
    httpd_resp_set_type(req, "text/html");
    len = sensor_config_html_end - sensor_config_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, sensor_config_html_start, len);
}

// POST handler: save bus1 config to /data/json/bus1_sensor_config.json
static esp_err_t sensorconfig_bus1_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_bus_config_from_body_urlencoded(buf, len, "b1_s");
    free(buf);

    char *out = cJSON_PrintUnformatted(arr);
    if (out)
    {
        // Print to UART using ESP-IDF logging
        LOGD(TAG, "JSON: %s", out);
        free(out);
    }

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
        LOGE(TAG, "Failed to reload bus1 config after write");
    }

    // After reconfig of sensors the sensors should re-init
    reload_and_init_sensors();

    // return the page body (original sensor_config.html)
    httpd_resp_set_type(req, "text/html");
    len = sensor_config_html_end - sensor_config_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, sensor_config_html_start, len);
}

// POST handler: restart ESP
static esp_err_t restart_esp_post_handler(httpd_req_t *req)
{
    size_t html_size = reboot_html_end - reboot_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, reboot_html_start, html_size);

    // Let the response flush
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_restart();
    return ESP_OK; // never reached
}

// POST handler: save wifi configuration to flash
static esp_err_t wifi_config_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_wifi_config_from_body_urlencoded(buf, len);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/wifi_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_wifi_config)
    if (load_wifi_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload wifi config after write");
    }

    network_manager_notify_config_change();

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    len = config_html_end - config_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save ethernet configuration to flash
static esp_err_t ethernet_config_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_ethernet_config_from_body_urlencoded(buf, len);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/ethernet_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_ethernet_config)
    if (load_ethernet_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload ethernet config after write");
    }

    network_manager_notify_config_change();

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    len = config_html_end - config_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save I2C configuration to flash
static esp_err_t i2c_config_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_i2c_config_from_body_urlencoded(buf, len);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/i2c_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_i2c_config)
    if (load_i2c_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload i2c config after write");
    }

    lcd_apply_config_change();

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save fan configuration to flash
static esp_err_t fan_config_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_fan_config_from_body_urlencoded(buf, len);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/fan_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_sensor_config)
    if (load_fan_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload fan config after write");
    }

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save influxdb configuration to flash
static esp_err_t influxdb_config_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_influxdb_config_from_body_urlencoded(buf, len);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/influxdb_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_sensor_config)
    if (load_influxdb_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload influxdb config after write");
    }

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save MQTT configuration to flash
static esp_err_t mqtt_config_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_mqtt_config_from_body_urlencoded(buf, len);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/mqtt_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_sensor_config)
    if (load_mqtt_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload MQTT config after write");
    }

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save syslog configuration to flash
static esp_err_t syslog_config_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_syslog_config_from_body_urlencoded(buf, len);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/syslog_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_sensor_config)
    if (load_syslog_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload syslog config after write");
    }

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save time configuration to flash
static esp_err_t time_config_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_time_config_from_body_urlencoded(buf, len);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/time_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_sensor_config)
    if (load_time_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload time config after write");
    }

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

static esp_err_t state_gen_config_post_handler(httpd_req_t *req)
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

    cJSON *arr = build_state_gen_config_from_body_urlencoded(buf, len);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    const char *path = "/data/json/sm_general_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // reload into runtime globals (uses mutex inside load_sensor_config)
    if (load_statemachine_general_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload statemachine general config after write");
    }

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    len = statemachine_html_end - statemachine_html_start;
    return httpd_resp_send(req, statemachine_html_start, len);
}

static esp_err_t state_config_post_handler(httpd_req_t *req)
{
    int state_number = (int)(intptr_t)req->user_ctx;
    char path[64];

    size_t len = req->content_len;

    LOGD(TAG, "Handling POST for state %d", state_number);
    LOGD(TAG, "Handling POST for state %d", state_number);

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

    cJSON *arr = build_state_config_from_body_urlencoded(buf, len, state_number);
    free(buf);
    if (!arr)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "parse fail");

    snprintf(path, sizeof(path), "/data/json/state%d_config.json", state_number);
    // const char *path = "/data/json/time_config.json";
    if (!write_json_atomic_fs(path, arr))
    {
        cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
    }
    cJSON_Delete(arr);

    // Reload into runtime globals (uses mutex inside load_sensor_config)
    if (load_state_config() != ESP_OK)
    {
        LOGE(TAG, "Failed to reload state config after write");
    }

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    len = statemachine_html_end - statemachine_html_start;
    return httpd_resp_send(req, statemachine_html_start, len);
}

// Set valve positions in NVS to 0
static esp_err_t reset_valves_handler(httpd_req_t *req)
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
        LOGE(TAG, "NVS open failed for reset_valves_handler: %d", err);
        // still return the page so UI stays consistent
    }

    // Update actual valve positions struct after writing all zeros to NVS, otherwise the statemachine will still have old values
    read_valve_pos_in_valvecontrol();

    // Return the original valvecontrol.html page
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    size_t len = valvecontrol_html_end - valvecontrol_html_start;
    return httpd_resp_send(req, valvecontrol_html_start, len);
}

// POST handler: stop the statemachine (set state to STOPPED)
static esp_err_t stop_statemachine_handler(httpd_req_t *req)
{
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_STOPPED;
        xSemaphoreGive(statemachine_mutex);
    }

    LOGI(TAG, "Statemachine stopped via web request");

    // return valvecontrol page so UI stays consistent
    httpd_resp_set_type(req, "text/html");
    size_t plen = valvecontrol_html_end - valvecontrol_html_start;
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, valvecontrol_html_start, plen);
}

// POST handler: start the statemachine (set state to INIT)
static esp_err_t start_statemachine_handler(httpd_req_t *req)
{
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_INIT;
        xSemaphoreGive(statemachine_mutex);
    }

    LOGI(TAG, "Statemachine started via web request");

    // return valvecontrol page so UI stays consistent
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    size_t plen = valvecontrol_html_end - valvecontrol_html_start;
    return httpd_resp_send(req, valvecontrol_html_start, plen);
}

// POST handler: update `valvecontroldata` from valvecontrol form and notify valve control task
static esp_err_t valvecontrol_post_handler(httpd_req_t *req)
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

    char key[64];
    char val[128];

    bool check_pos = false;
    bool store_pos = false;

    // parse check/store checkboxes
    if (httpd_query_key_value(buf, "check_valve_position", val, sizeof(val)) == ESP_OK)
    {
        url_decode_inplace(val);
        check_pos = (strcasecmp(val, "true") == 0 || strcmp(val, "On") == 0 || strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
    }

    if (httpd_query_key_value(buf, "store_valve_position_in_file", val, sizeof(val)) == ESP_OK)
    {
        url_decode_inplace(val);
        store_pos = (strcasecmp(val, "true") == 0 || strcmp(val, "On") == 0 || strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
    }

    // Write values into global valvecontroldata under mutex
    if (valve_control_data_mutex && xSemaphoreTake(valve_control_data_mutex, (TickType_t)10) == pdTRUE)
    {
        for (int i = 0; i < MAX_VALVE_COUNT; ++i)
        {
            // default assignments
            valvecontroldata.valve_number[i] = i;
            valvecontroldata.position_change[i] = 0;
            valvecontroldata.direction[i] = 1; // default to Open

            // position
            snprintf(key, sizeof(key), "valve%d_position_move", i);
            if (httpd_query_key_value(buf, key, val, sizeof(val)) == ESP_OK)
            {
                url_decode_inplace(val);
                int pos = atoi(val);
                if (pos < 0)
                    pos = 0;
                if (pos > 24)
                    pos = 24;
                valvecontroldata.position_change[i] = pos;
            }

            // direction
            snprintf(key, sizeof(key), "valve%d_direction", i);
            if (httpd_query_key_value(buf, key, val, sizeof(val)) == ESP_OK)
            {
                url_decode_inplace(val);
                // Accept 'Open' or 'Close' (case-insensitive)
                if (strcasecmp(val, "open") == 0)
                    valvecontroldata.direction[i] = 1;
                else if (strcasecmp(val, "close") == 0)
                    valvecontroldata.direction[i] = 0;
                else
                    valvecontroldata.direction[i] = 1; // fallback
            }

            // keep actual_valve_position untouched here
        }

        valvecontroldata.check_position = check_pos;
        valvecontroldata.write_new_position = store_pos;
        valvecontroldata.valve_move_request_from_post = true;

        xSemaphoreGive(valve_control_data_mutex);
    }

    // notify valve control task to carry out moves
    if (task_valvectrl)
    {
        xTaskNotifyGive(task_valvectrl);
    }
    else
    {
        LOGE(TAG, "Valve control task handle not present - cannot notify");
    }

    free(buf);

    // Return the original valvecontrol.html page
    httpd_resp_set_type(req, "text/html");
    net_mgr_notify_eth_activity();
    size_t plen = valvecontrol_html_end - valvecontrol_html_start;
    return httpd_resp_send(req, valvecontrol_html_start, plen);
}

static esp_err_t handle_get_all_configs_tar(httpd_req_t *req)
{
    DIR *dir = opendir("/data/json");
    if (!dir)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot open config directory");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/x-tar");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"configs.tar\"");

    struct dirent *entry;
    uint8_t buf[512];
    char path[300];

    while ((entry = readdir(dir)) != NULL)
    {
        if (!strstr(entry->d_name, ".json"))
            continue;

        snprintf(path, sizeof(path), "/data/json/%s", entry->d_name);

        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        // build tar header locally and send it raw
        tar_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        strncpy(hdr.name, entry->d_name, sizeof(hdr.name) - 1);
        hdr.name[sizeof(hdr.name) - 1] = '\0';
        snprintf(hdr.mode, sizeof(hdr.mode), "%07o", 0644);
        snprintf(hdr.uid, sizeof(hdr.uid), "%07o", 0);
        snprintf(hdr.gid, sizeof(hdr.gid), "%07o", 0);
        snprintf(hdr.size, sizeof(hdr.size), "%011o", (unsigned int)size);
        snprintf(hdr.mtime, sizeof(hdr.mtime), "%011o", (unsigned int)time(NULL));
        memcpy(hdr.magic, "ustar", 5);
        memcpy(hdr.version, "00", 2);
        hdr.typeflag = '0';
        tar_fill_checksum(&hdr);

        httpd_resp_send_chunk(req, (const char *)&hdr, 512);

        long remaining = size;
        while (remaining > 0)
        {
            size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            size_t r = fread(buf, 1, chunk, f);
            if (r == 0)
                break;
            httpd_resp_send_chunk(req, (const char *)buf, r);
            remaining -= r;
        }

        fclose(f);

        size_t pad = (512 - (size % 512)) % 512;
        if (pad > 0)
        {
            memset(buf, 0, pad);
            httpd_resp_send_chunk(req, (const char *)buf, pad);
        }
    }

    closedir(dir);

    memset(buf, 0, 512);
    httpd_resp_send_chunk(req, (const char *)buf, 512);
    httpd_resp_send_chunk(req, (const char *)buf, 512);

    httpd_resp_send_chunk(req, NULL, 0);
    net_mgr_notify_eth_activity();
    return ESP_OK;
}

static esp_err_t handle_import_all_configs_tar(httpd_req_t *req)
{
    // Read entire uploaded file into RAM (tar is small ~23 KB)
    size_t total = req->content_len;
    if (total == 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty upload");

    uint8_t *tarbuf = malloc(total);
    if (!tarbuf)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");

    size_t rec = 0;
    while (rec < total)
    {
        int r = httpd_req_recv(req, (char *)tarbuf + rec, total - rec);
        if (r <= 0)
        {
            free(tarbuf);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv error");
        }
        rec += r;
    }

    // Parse tar entries
    size_t pos = 0;
    while (pos + 512 <= total)
    {
        tar_header_t *hdr = (tar_header_t *)(tarbuf + pos);

        // End of archive: two 512-byte zero blocks
        bool all_zero = true;
        for (int i = 0; i < 512; i++)
            if (tarbuf[pos + i] != 0)
            {
                all_zero = false;
                break;
            }

        if (all_zero)
            break;

        // Extract filename
        char name[101];
        memcpy(name, hdr->name, 100);
        name[100] = '\0';

        // Only import JSON files
        if (strstr(name, ".json"))
        {
            // Parse file size (octal)
            size_t filesize = strtol(hdr->size, NULL, 8);

            // Compute file data offset
            size_t data_offset = pos + 512;
            if (data_offset + filesize > total)
            {
                free(tarbuf);
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "tar truncated");
            }

            // Build full path
            char path[256];
            snprintf(path, sizeof(path), "/data/json/%s", name);

            // Write file atomically
            FILE *f = fopen("/data/json/tmp_import.json", "w");
            if (!f)
            {
                free(tarbuf);
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
            }

            fwrite(tarbuf + data_offset, 1, filesize, f);
            fclose(f);

            rename("/data/json/tmp_import.json", path);
        }

        // Move to next entry (header + padded data)
        size_t filesize = strtol(hdr->size, NULL, 8);
        size_t padded = ((filesize + 511) / 512) * 512;
        pos += 512 + padded;
    }

    free(tarbuf);

    // Reload all configs
    load_wifi_config();
    load_ethernet_config();
    load_i2c_config();
    load_fan_config();
    load_mqtt_config();
    load_syslog_config();
    load_time_config();
    load_sensor_config("bus0_sensor_config");
    load_sensor_config("bus1_sensor_config");
    load_state_config();
    load_statemachine_general_config();

    char referer[128] = {0};
    size_t referer_len = httpd_req_get_hdr_value_len(req, "Referer");

    if (referer_len > 0 && referer_len < sizeof(referer))
    {
        httpd_req_get_hdr_value_str(req, "Referer", referer, sizeof(referer));

        // Convert absolute URL → path only
        char *path = strstr(referer, "://");
        if (path)
        {
            path = strchr(path + 3, '/'); // first slash after hostname
            if (path)
                memmove(referer, path, strlen(path) + 1);
            else
                strcpy(referer, "/config.html");
        }
    }
    else
    {
        strcpy(referer, "/config.html");
    }

    // Return config page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", referer);
    net_mgr_notify_eth_activity();
    return httpd_resp_send(req, NULL, 0);
}

void ws_async_send(char *buf, size_t len)
{

    LOGD(TAG, "ws_async_send: called with %zu bytes", len);
    if (http_server == NULL)
    {
        LOGW(TAG, "ws_async_send: http_server is NULL, not sending");
        return;
    }

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
    LOGD(TAG, "ws_async_send: httpd_get_client_list found %zu clients, ret=%d", fds, ret);
    if (ret != ESP_OK)
    {
        LOGW(TAG, "ws_async_send: httpd_get_client_list failed");
        return;
    }

    int sent = 0;
    for (int i = 0; i < fds; i++)
    {
        int client_info = httpd_ws_get_fd_info(http_server, client_fds[i]);
        LOGD(TAG, "ws_async_send: client_fd[%d]=%d info=%d", i, client_fds[i], client_info);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET)
        {
            esp_err_t sret = httpd_ws_send_frame_async(http_server, client_fds[i], &ws_pkt);
            LOGD(TAG, "ws_async_send: sent to client_fd[%d]=%d, sret=%d", i, client_fds[i], sret);
            sent++;
        }
    }
    if (sent == 0)
    {
        LOGW(TAG, "ws_async_send: No WebSocket clients to send to");
    }
}

esp_err_t http_server_start(void)
{
    if (http_server)
    {
        LOGW(TAG, "HTTP server already running, skipping start");
        return ESP_OK;
    }

    esp_netif_t *netif = get_active_netif();
    if (!netif)
    {
        LOGW(TAG, "No active network interface, delaying HTTP server start");
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0)
    {
        LOGW(TAG, "Active interface has no IP yet, delaying HTTP server start");
        return ESP_ERR_INVALID_STATE;
    }

    LOGI(TAG, "Starting HTTP server on IP: " IPSTR, IP2STR(&ip.ip));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 50;
    config.max_open_sockets = 16;
    config.stack_size = 6144;
    config.task_priority = tskIDLE_PRIORITY + 20;

    esp_err_t err = httpd_start(&http_server, &config);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    register_http_handlers(http_server);
    LOGI(TAG, "HTTP server started successfully");
    return ESP_OK;
}

void register_http_handlers(httpd_handle_t server)
{
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

    httpd_uri_t send_json_ws_uri = {
        .uri = "/send_json_ws.js",
        .method = HTTP_GET,
        .handler = send_json_ws_js_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &send_json_ws_uri);

    httpd_uri_t config_uri = {
        .uri = "/configuration",
        .method = HTTP_GET,
        .handler = config_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &config_uri);

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

    httpd_uri_t valvecontrol_post_uri = {
        .uri = "/valvecontrol",
        .method = HTTP_POST,
        .handler = valvecontrol_post_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &valvecontrol_post_uri);

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

    // POST URI to restart the ESP
    httpd_uri_t restart_esp_uri = {
        .uri = "/restart_esp",
        .method = HTTP_POST,
        .handler = restart_esp_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &restart_esp_uri);

    // POST URI to stop the statemachine from the Valve Control page
    httpd_uri_t stop_statemachine_uri = {
        .uri = "/stop_statemachine",
        .method = HTTP_POST,
        .handler = stop_statemachine_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &stop_statemachine_uri);

    // POST URI to start the statemachine from the Valve Control page
    httpd_uri_t start_statemachine_uri = {
        .uri = "/start_statemachine",
        .method = HTTP_POST,
        .handler = start_statemachine_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &start_statemachine_uri);

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

    httpd_uri_t wifi_config_post_uri = {
        .uri = "/wifi_config",
        .method = HTTP_POST,
        .handler = wifi_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &wifi_config_post_uri);

    httpd_uri_t ethernet_config_post_uri = {
        .uri = "/ethernet_config",
        .method = HTTP_POST,
        .handler = ethernet_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &ethernet_config_post_uri);

    httpd_uri_t i2c_config_post_uri = {
        .uri = "/i2c_config",
        .method = HTTP_POST,
        .handler = i2c_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &i2c_config_post_uri);

    httpd_uri_t fan_config_post_uri = {
        .uri = "/fan_config",
        .method = HTTP_POST,
        .handler = fan_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &fan_config_post_uri);

    httpd_uri_t influxdb_config_post_uri = {
        .uri = "/influxdb_config",
        .method = HTTP_POST,
        .handler = influxdb_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &influxdb_config_post_uri);

    httpd_uri_t mqtt_config_post_uri = {
        .uri = "/mqtt_config",
        .method = HTTP_POST,
        .handler = mqtt_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &mqtt_config_post_uri);

    httpd_uri_t syslog_config_post_uri = {
        .uri = "/syslog_config",
        .method = HTTP_POST,
        .handler = syslog_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &syslog_config_post_uri);

    httpd_uri_t time_config_post_uri = {
        .uri = "/time_config",
        .method = HTTP_POST,
        .handler = time_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &time_config_post_uri);

    httpd_uri_t state_gen_config_post_uri = {
        .uri = "/state_gen_config",
        .method = HTTP_POST,
        .handler = state_gen_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &state_gen_config_post_uri);

    httpd_uri_t state0_config_post_uri = {
        .uri = "/state0_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)0, // pass state number 0
    };
    httpd_register_uri_handler(http_server, &state0_config_post_uri);

    httpd_uri_t state1_config_post_uri = {
        .uri = "/state1_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)1, // pass state number 0
    };
    httpd_register_uri_handler(http_server, &state1_config_post_uri);

    httpd_uri_t state2_config_post_uri = {
        .uri = "/state2_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)2,
    };
    httpd_register_uri_handler(http_server, &state2_config_post_uri);

    httpd_uri_t state3_config_post_uri = {
        .uri = "/state3_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)0,
    };
    httpd_register_uri_handler(http_server, &state3_config_post_uri);

    httpd_uri_t state4_config_post_uri = {
        .uri = "/state4_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)4,
    };
    httpd_register_uri_handler(http_server, &state4_config_post_uri);

    httpd_uri_t state5_config_post_uri = {
        .uri = "/state5_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)5,
    };
    httpd_register_uri_handler(http_server, &state5_config_post_uri);

    httpd_uri_t state6_config_post_uri = {
        .uri = "/state6_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)6,
    };
    httpd_register_uri_handler(http_server, &state6_config_post_uri);

    httpd_uri_t state7_config_post_uri = {
        .uri = "/state7_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)7,
    };
    httpd_register_uri_handler(http_server, &state7_config_post_uri);

    httpd_uri_t state8_config_post_uri = {
        .uri = "/state8_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)8,
    };
    httpd_register_uri_handler(http_server, &state8_config_post_uri);

    httpd_uri_t state9_config_post_uri = {
        .uri = "/state9_config",
        .method = HTTP_POST,
        .handler = state_config_post_handler,
        .user_ctx = (void *)9,
    };
    httpd_register_uri_handler(http_server, &state9_config_post_uri);

    httpd_uri_t exp_all_config_tar_uri = {
        .uri = "/api/config/all.tar",
        .method = HTTP_GET,
        .handler = handle_get_all_configs_tar,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &exp_all_config_tar_uri);

    httpd_uri_t imp_all_config_tar_uri = {
        .uri = "/api/config/import",
        .method = HTTP_POST,
        .handler = handle_import_all_configs_tar,
        .user_ctx = NULL};
    httpd_register_uri_handler(http_server, &imp_all_config_tar_uri);
}

esp_err_t http_server_stop(void)
{
    int ret;

    LOGW(TAG, "Stopping HTTP server");

    if (http_server)
    {
        ret = httpd_stop(http_server);
        if (ret == ESP_OK)
        {
            http_server = NULL;
        }
    }
    else
    {
        ret = ESP_ERR_INVALID_ARG;
    }
    return ret;
}
