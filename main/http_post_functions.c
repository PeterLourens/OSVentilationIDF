#include "http_post_functions.h"

static const char *TAG = "http post json";

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

    snprintf(key, sizeof(key), "bus0_multiplexer_address");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "bus1_multiplexer_address");
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

    // ESP_LOGI("JSON builder", "I2C config: %s", obj);

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

/*cJSON *build_time_config_from_body_urlencoded(const char *body, size_t len)
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
}*/

// Inline URL decode helper static
void url_decode_inplace(char *str)
{
    char *src = str, *dst = str;
    while (*src)
    {
        if (*src == '%' && src[1] && src[2])
        {
            char hex[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

cJSON *build_time_config_from_body_urlencoded(const char *body, size_t len)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char val[256];

    if (httpd_query_key_value(body, "ntp_server", val, sizeof(val)) == ESP_OK)
    {
        url_decode_inplace(val);
        cJSON_AddStringToObject(obj, "ntp_server", val);
    }

    if (httpd_query_key_value(body, "timezone", val, sizeof(val)) == ESP_OK)
    {
        url_decode_inplace(val);
        cJSON_AddStringToObject(obj, "timezone", val);
    }

    return obj;
}

cJSON *build_state_gen_config_from_body_urlencoded(const char *body, size_t len)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[64], val[256];

    snprintf(key, sizeof(key), "wday_d_h_start");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "wday_d_m_start");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "wday_n_h_start");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "wday_n_m_start");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "wkend_d_h_start");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "wkend_d_m_start");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");
    snprintf(key, sizeof(key), "wkend_n_h_start");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");
    snprintf(key, sizeof(key), "wkend_n_m_start");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "wkend_d1");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "wkend_d2");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, key, "");

    snprintf(key, sizeof(key), "rh_l");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
    {
        int rh_low = atoi(val);
        cJSON_AddNumberToObject(obj, "rh_low_level", rh_low);
    }
    else
        cJSON_AddNumberToObject(obj, "rh_low_level", 0);

    snprintf(key, sizeof(key), "rh_h");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
    {
        int rh_high = atoi(val);
        cJSON_AddNumberToObject(obj, "rh_high_level", rh_high);
    }
    else
        cJSON_AddNumberToObject(obj, "rh_high_level", 0);

    snprintf(key, sizeof(key), "co2_h");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
    {
        int co2_high = atoi(val);
        cJSON_AddNumberToObject(obj, "co2_high_level", co2_high);
    }
    else
        cJSON_AddNumberToObject(obj, "co2_high_level", 0);

    snprintf(key, sizeof(key), "co2_l");
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
    {
        int co2_low = atoi(val);
        cJSON_AddNumberToObject(obj, "co2_low_level", co2_low);
    }
    else
        cJSON_AddNumberToObject(obj, "co2_low_level", 0);
    return obj;
}

cJSON *build_state_config_from_body_urlencoded(const char *body, size_t len, int state_number)
{
    if (!body)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    char key[16];
    char key_new[32];
    char val[64];
    int fanspeed_code = 0;

    snprintf(key, sizeof(key), "s%d_en", state_number);
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, "enable_state", (strcmp(val, "On") == 0) ? "On" : "Off");
    else
        cJSON_AddStringToObject(obj, "enable_state", "Off");

    /*snprintf(key, sizeof(key), "s%d_fanspeed", state_number);
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, "fanspeed", val);
    else
        cJSON_AddStringToObject(obj, "fanspeed", "0");*/

    // Map fanspeed string to code, Low=1, Medium=2, High=3, Error=0
    snprintf(key, sizeof(key), "s%d_fanspeed", state_number);
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
    {
        if (strcmp(val, "Low") == 0)
            fanspeed_code = 1;
        else if (strcmp(val, "Medium") == 0)
            fanspeed_code = 2;
        else if (strcmp(val, "High") == 0)
            fanspeed_code = 3;
        else
            fanspeed_code = 0; // unexpected value
    }
    cJSON_AddNumberToObject(obj, "fanspeed", fanspeed_code);

    snprintf(key, sizeof(key), "s%d_name", state_number);
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, "state_name", val);
    else
        cJSON_AddStringToObject(obj, "state_name", "");

    snprintf(key, sizeof(key), "s%d_st_h", state_number);
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, "state_start_hour", val);
    else
        cJSON_AddStringToObject(obj, "state_start_hour", "");

    snprintf(key, sizeof(key), "s%d_st_m", state_number);
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, "state_start_minute", val);
    else
        cJSON_AddStringToObject(obj, "state_start_minute", "");

    snprintf(key, sizeof(key), "s%d_end_h", state_number);
    if (httpd_query_key_value(body, "state_end_hour", val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddStringToObject(obj, "state_end_hour", "");

    snprintf(key, sizeof(key), "s%d_end_m", state_number);
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, "state_end_minute", val);
    else
        cJSON_AddStringToObject(obj, "state_end_minute", "");

    snprintf(key, sizeof(key), "s%d_dur_m", state_number);
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, "duration_minutes", val);
    else
        cJSON_AddStringToObject(obj, "duration_minutes", "");

    snprintf(key, sizeof(key), "s%d_cycl_int_m", state_number);
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
        cJSON_AddStringToObject(obj, "cycling_interval_min", val);
    else
        cJSON_AddStringToObject(obj, "cycling_interval_min", "");

    for (int j = 0; j < MAX_VALVE_COUNT; ++j)
    {
        snprintf(key, sizeof(key), "s%d_v%d_pos", state_number, j);
        snprintf(key_new, sizeof(key_new), "valve%d_pos", j);
        if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK)
            cJSON_AddStringToObject(obj, key_new, val);
        else
            cJSON_AddStringToObject(obj, key_new, "");
    }

    char *out = cJSON_PrintUnformatted(obj);
    ESP_LOGI(TAG, "State %d config: %s", state_number, out ? out : "NULL");

    return obj;
}