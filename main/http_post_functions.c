#include "http_post_functions.h"

// Build a JSON array for one bus from urlencoded POST body.
// field naming in the HTML: bN_sX_<type|valve|location|rh|co2>
cJSON *build_bus_config_from_body_urlencoded(const char *body, size_t len, const char *bus_prefix)
{
    if (!body || !bus_prefix)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[64], val[256];

    for (int ch = 0; ch < CHANNELS_PER_BUS; ++ch)
    {
        /* type */
        snprintf(key, sizeof(key), "%s%d_type", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddStringToObject(obj, key, val);
        else
            cJSON_AddStringToObject(obj, key, "None");

        /* valve */
        snprintf(key, sizeof(key), "%s%d_valve", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddStringToObject(obj, key, val);
        else
            cJSON_AddStringToObject(obj, key, "None");

        /* location */
        snprintf(key, sizeof(key), "%s%d_location", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddStringToObject(obj, key, val);
        else
            cJSON_AddStringToObject(obj, key, "");

        /* RH control (On/Off) */
        snprintf(key, sizeof(key), "%s%d_rh", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddStringToObject(obj, key, (strcmp(val, "On") == 0) ? "On" : "Off");
        else
            cJSON_AddStringToObject(obj, key, "Off");

        /* CO2 control (On/Off) */
        snprintf(key, sizeof(key), "%s%d_co2", bus_prefix, ch);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddStringToObject(obj, key, (strcmp(val, "On") == 0) ? "On" : "Off");
        else
            cJSON_AddStringToObject(obj, key, "Off");
    }

    return obj;
}

cJSON *build_network_config_from_body_urlencoded(const char *body, size_t len)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[64], val[256];

    snprintf(key, sizeof(key), "enable_dhcp");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, (strcmp(val, "On") == 0) ? "On" : "Off");
    else
        cJSON_AddStringToObject(obj, key, "None");

    snprintf(key, sizeof(key), "ssid");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "wifi_password");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "ip_address");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "subnet_mask");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "gateway");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "primary_dns");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "secondary_dns");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    return obj;
}

cJSON *build_i2c_config_from_body_urlencoded(const char *body, size_t len)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[64], val[256];

    snprintf(key, sizeof(key), "bus0_multiplpexer_address");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "bus1_multiplpexer_address");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "enable_lcd");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, (strcmp(val, "On") == 0) ? "On" : "Off");
    else
        cJSON_AddStringToObject(obj, key, "");

    /*snprintf(key, sizeof(key), "lcd_i2c_address");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");*/

    return obj;
}

cJSON *build_mqtt_config_from_body_urlencoded(const char *body, size_t len)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[64], val[256];

    snprintf(key, sizeof(key), "enable_mqtt");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, (strcmp(val, "On") == 0) ? "On" : "Off");
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "mqtt_server");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "mqtt_port");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "mqtt_base_topic");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    return obj;
}

cJSON *build_influxdb_config_from_body_urlencoded(const char *body, size_t len)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[64], val[256];

    snprintf(key, sizeof(key), "enable_influxdb");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, (strcmp(val, "On") == 0) ? "On" : "Off");
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "influxdb_url");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "influxdb_org");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "influxdb_bucket");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "influxdb_token");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    return obj;
}

cJSON *build_fan_config_from_body_urlencoded(const char *body, size_t len)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[64], val[256];

    snprintf(key, sizeof(key), "fan_control_mode");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "fan_control_mqtt_server");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "fan_control_mqtt_port");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "fan_control_mqtt_topic");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "fan_control_url_hs");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "fan_control_url_ms");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "fan_control_url_ls");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    return obj;
}

cJSON *build_syslog_config_from_body_urlencoded(const char *body, size_t len)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[64], val[256];

    snprintf(key, sizeof(key), "enable_syslog");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, (strcmp(val, "On") == 0) ? "On" : "Off");
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "syslog_ip");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "syslog_port");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    return obj;
}

cJSON *build_time_config_from_body_urlencoded(const char *body, size_t len)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[64], val[256];

    snprintf(key, sizeof(key), "ntp_server");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "timezone");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    return obj;
}