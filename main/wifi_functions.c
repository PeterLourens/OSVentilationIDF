#include "wifi_functions.h"

static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *ap_netif = NULL;
static int s_retry_num = 0;
#define MAXIMUM_RETRY 5

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry_num < MAXIMUM_RETRY)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "Retry to connect to the AP");
            }
            else
            {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP Started");
            xEventGroupSetBits(s_wifi_event_group, AP_STARTED_BIT);
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                     MAC2STR(event->mac), event->aid);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            wifi_event_ap_stadisconnected_t *event_disc = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d",
                     MAC2STR(event_disc->mac), event_disc->aid);
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create both AP and STA netif
    esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_AP_SSID,
            .ssid_len = strlen(ESP_AP_SSID),
            .channel = ESP_AP_CHANNEL,
            .password = ESP_AP_PASS,
            .max_connection = ESP_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false},
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started with SSID: %s", ESP_AP_SSID);

    // Wait for AP to start
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           AP_STARTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & AP_STARTED_BIT)
    {
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t wifi_manager_connect(Network_settings *settings)
{
    if (!settings)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // If DHCP is disabled, start AP mode
    if (strcmp(settings->enable_dhcp, "false") == 0)
    {
        ESP_LOGI(TAG, "DHCP disabled, starting AP mode");
        return wifi_manager_start_ap();
    }

    // Station mode configuration
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };

    strncpy((char *)wifi_config.sta.ssid, settings->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, settings->wifi_password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_manager_connect finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to ap SSID:%s", settings->ssid);
        return ESP_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", settings->ssid);
        return ESP_FAIL;
    }

    return ESP_ERR_INVALID_STATE;
}

esp_err_t wifi_manager_disconnect(void)
{
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_stop());
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t wifi_manager_get_ip_info(char *ip_str, size_t max_len)
{
    if (!ip_str || max_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif)
    {
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
    {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
        return ESP_OK;
    }
    return ESP_FAIL;
}