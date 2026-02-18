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

// Serve send_json_ws.js
esp_err_t send_json_ws_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    size_t len = send_json_ws_js_end - send_json_ws_js_start - 1;
    return httpd_resp_send(req, send_json_ws_js_start, len);
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

// Serve config.html
esp_err_t config_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
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
        LOGD(TAG, "Handshake done, the new connection was opened");
        http_server = req->handle;
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

        if (strncmp((char *)ws_pkt.payload, "Start", 5) == 0)
        {
            // Redirect log output to the custom function
            esp_log_set_vprintf(websocket_log_output);
            LOGD(TAG, "Redirecting console to websocket");
        }

        if (strncmp((char *)ws_pkt.payload, "Stop", 4) == 0)
        {
            // Redirect log output to the custom function
            LOGD(TAG, "Stopped redirecting console to websocket");
            esp_log_set_vprintf(uart_log_output);
        }

        if (strncmp((char *)ws_pkt.payload, "stats", 8) == 0)
        {
            LOGD(TAG, "Stats requested via WebSocket");
            show_system_and_task_stats();
        }

        if (strncmp((char *)ws_pkt.payload, "index", 9) == 0)
        {
            LOGD(TAG, "Index data requested via WebSocket");
            send_index_html_ws();
        }

        if (strncmp((char *)ws_pkt.payload, "sensor_config", 17) == 0)
        {
            LOGD(TAG, "Sensor config requested via WebSocket");
            send_sensor_config_json_ws();
        }

        if (strncmp((char *)ws_pkt.payload, "config", 10) == 0)
        {
            LOGD(TAG, "Config requested via WebSocket");
            send_config_json_ws();
        }
        if (strncmp((char *)ws_pkt.payload, "statemachine", 10) == 0)
        {
            LOGD(TAG, "Statemachine config requested via WebSocket");
            send_statemachine_config_json_ws();
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
    return httpd_resp_send(req, sensor_config_html_start, len);
}

/* POST handler: restart ESP */
esp_err_t restart_esp_post_handler(httpd_req_t *req)
{
    size_t html_size = reboot_html_end - reboot_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, reboot_html_start, html_size);

    // Let the response flush
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_restart();
    return ESP_OK; // never reached
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

// POST handler: save wifi configuration to flash
esp_err_t wifi_config_post_handler(httpd_req_t *req)
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

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save ethernet configuration to flash
esp_err_t ethernet_config_post_handler(httpd_req_t *req)
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

    // return the page body (original config.html)
    httpd_resp_set_type(req, "text/html");
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save I2C configuration to flash
esp_err_t i2c_config_post_handler(httpd_req_t *req)
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
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save fan configuration to flash
esp_err_t fan_config_post_handler(httpd_req_t *req)
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
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save influxdb configuration to flash
esp_err_t influxdb_config_post_handler(httpd_req_t *req)
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
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save MQTT configuration to flash
esp_err_t mqtt_config_post_handler(httpd_req_t *req)
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
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save syslog configuration to flash
esp_err_t syslog_config_post_handler(httpd_req_t *req)
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
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

// POST handler: save time configuration to flash
esp_err_t time_config_post_handler(httpd_req_t *req)
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
    len = config_html_end - config_html_start;
    return httpd_resp_send(req, config_html_start, len);
}

esp_err_t state_gen_config_post_handler(httpd_req_t *req)
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
    len = statemachine_html_end - statemachine_html_start;
    return httpd_resp_send(req, statemachine_html_start, len);
}

esp_err_t state_config_post_handler(httpd_req_t *req)
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
    len = statemachine_html_end - statemachine_html_start;
    return httpd_resp_send(req, statemachine_html_start, len);
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
        LOGE(TAG, "NVS open failed for reset_valves_handler: %d", err);
        // still return the page so UI stays consistent
    }

    // Update actual valve positions struct after writing all zeros to NVS, otherwise the statemachine will still have old values
    read_valve_pos_in_valvecontrol();

    // Return the original valvecontrol.html page
    httpd_resp_set_type(req, "text/html");
    size_t len = valvecontrol_html_end - valvecontrol_html_start;
    return httpd_resp_send(req, valvecontrol_html_start, len);
}

// POST handler: stop the statemachine (set state to STOPPED)
esp_err_t stop_statemachine_handler(httpd_req_t *req)
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
    return httpd_resp_send(req, valvecontrol_html_start, plen);
}

// POST handler: start the statemachine (set state to INIT)
esp_err_t start_statemachine_handler(httpd_req_t *req)
{
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_INIT;
        xSemaphoreGive(statemachine_mutex);
    }

    LOGI(TAG, "Statemachine started via web request");

    // return valvecontrol page so UI stays consistent
    httpd_resp_set_type(req, "text/html");
    size_t plen = valvecontrol_html_end - valvecontrol_html_start;
    return httpd_resp_send(req, valvecontrol_html_start, plen);
}

// POST handler: update `valvecontroldata` from valvecontrol form and notify valve control task
esp_err_t valvecontrol_post_handler(httpd_req_t *req)
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
    size_t plen = valvecontrol_html_end - valvecontrol_html_start;
    return httpd_resp_send(req, valvecontrol_html_start, plen);
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
    config.max_uri_handlers = 50;                 // increase from default 8 to 50
    config.stack_size = 6144;                     // increase stack size for websocket handling
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

    return http_server;
}

esp_err_t http_server_stop(void)
{
    int ret;

    LOGW(TAG, "Stopping HTTP server");

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
