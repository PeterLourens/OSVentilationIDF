#include "fancontrol.h"

#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_err.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "fancontrol";

static const char *fanspeed_to_str(int s)
{
    switch (s)
    {
    case FANSPEED_LOW:
        return "Low";
    case FANSPEED_MEDIUM:
        return "Medium";
    case FANSPEED_HIGH:
        return "High";
    case FANSPEED_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

/* Helper: perform a simple HTTP GET and capture the response body (truncated to out_buf_len-1)
 * Returns HTTP status code on success, or a negative error code on failure.
 */
static int do_http_get(const char *url, char *out_buf, size_t out_buf_len)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client for %s", url);
        return -1;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP GET request failed for %s: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    int status = esp_http_client_get_status_code(client);

    if (out_buf && out_buf_len > 0)
    {
        int r = esp_http_client_read(client, out_buf, out_buf_len - 1);
        if (r > 0)
        {
            out_buf[r] = '\0';
        }
        else
        {
            out_buf[0] = '\0';
        }
    }

    esp_http_client_cleanup(client);
    return status;
}

void set_fanspeed(int fanspeed)
{
    int httpResponseCode = 0;

    char fan_control_url_high_speed[XLARGE_CONFIG_ITEM];
    char fan_control_url_medium_speed[XLARGE_CONFIG_ITEM];
    char fan_control_url_low_speed[XLARGE_CONFIG_ITEM];
    char fan_control_mode[MEDIUM_CONFIG_ITEM];
    char fan_control_mqtt_topic[XLARGE_CONFIG_ITEM];
    char http_payload[XLARGE_CONFIG_ITEM] = "";

    /* Use integer fanspeed passed in (0=error,1=low,2=medium,3=high) */
    int cur_speed = fanspeed;

    /* Read fan control config under fan_config_mutex */
    if (fan_config_mutex && xSemaphoreTake(fan_config_mutex, (TickType_t)50) == pdTRUE)
    {
        strncpy(fan_control_mode, fan_config.fan_control_mode, sizeof(fan_control_mode) - 1);
        fan_control_mode[sizeof(fan_control_mode) - 1] = '\0';

        strncpy(fan_control_mqtt_topic, fan_config.fan_control_mqtt_topic, sizeof(fan_control_mqtt_topic) - 1);
        fan_control_mqtt_topic[sizeof(fan_control_mqtt_topic) - 1] = '\0';

        strncpy(fan_control_url_high_speed, fan_config.fan_control_url_hs, sizeof(fan_control_url_high_speed) - 1);
        fan_control_url_high_speed[sizeof(fan_control_url_high_speed) - 1] = '\0';

        strncpy(fan_control_url_medium_speed, fan_config.fan_control_url_ms, sizeof(fan_control_url_medium_speed) - 1);
        fan_control_url_medium_speed[sizeof(fan_control_url_medium_speed) - 1] = '\0';

        strncpy(fan_control_url_low_speed, fan_config.fan_control_url_ls, sizeof(fan_control_url_low_speed) - 1);
        fan_control_url_low_speed[sizeof(fan_control_url_low_speed) - 1] = '\0';

        xSemaphoreGive(fan_config_mutex);
    }

    ESP_LOGI(TAG, "Fanspeed %s", fanspeed_to_str(cur_speed));

    if (strcmp(fan_control_mode, "MQTT publish") == 0)
    {
        ESP_LOGI(TAG, "Using MQTT to set fan speed");
        /* Not yet implemented: subscribe/publish to MQTT topic */
    }
    else if (strcmp(fan_control_mode, "HTTP API") == 0)
    {
        ESP_LOGI(TAG, "Using HTTP API to set fan speed");

        if (cur_speed == FANSPEED_LOW)
        {
            ESP_LOGI(TAG, "Low speed selected");
            httpResponseCode = do_http_get(fan_control_url_low_speed, http_payload, sizeof(http_payload));
            if (httpResponseCode > 0)
            {
                ESP_LOGI(TAG, "HTTP Response code: %d, HTTP payload: %s", httpResponseCode, http_payload);
            }
            else
            {
                ESP_LOGE(TAG, "HTTP request failed for Low speed (code %d)", httpResponseCode);
            }
        }
        else if (cur_speed == FANSPEED_MEDIUM)
        {
            ESP_LOGI(TAG, "Medium speed selected");
            httpResponseCode = do_http_get(fan_control_url_medium_speed, http_payload, sizeof(http_payload));
            if (httpResponseCode > 0)
            {
                ESP_LOGI(TAG, "HTTP Response code: %d, HTTP payload: %s", httpResponseCode, http_payload);
            }
            else
            {
                ESP_LOGE(TAG, "HTTP request failed for Medium speed (code %d)", httpResponseCode);
            }
        }
        else if (cur_speed == FANSPEED_HIGH)
        {
            ESP_LOGI(TAG, "High speed selected");
            httpResponseCode = do_http_get(fan_control_url_high_speed, http_payload, sizeof(http_payload));
            if (httpResponseCode > 0)
            {
                ESP_LOGI(TAG, "HTTP Response code: %d, HTTP payload: %s", httpResponseCode, http_payload);
            }
            else
            {
                ESP_LOGE(TAG, "HTTP request failed for High speed (code %d)", httpResponseCode);
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "No method to set fanspeed");
    }
}
