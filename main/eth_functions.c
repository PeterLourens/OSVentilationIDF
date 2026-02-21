/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "eth_functions.h"

static const char *TAG = "Eth_func";

esp_netif_t *eth_netif = NULL;
esp_eth_handle_t eth_handle = NULL;

// Internal state
static volatile bool eth_connected = false;

void eth_init(void)
{
    esp_err_t err;

    LOGI(TAG, "eth_init: initializing W5500 Ethernet");

    /* Create Ethernet netif (do not call esp_netif_init() here) */
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&cfg);
    if (eth_netif == NULL)
    {
        LOGE(TAG, "esp_netif_new(ESP_NETIF_DEFAULT_ETH) returned NULL");
        return;
    }

    // ----------------- SPI bus init -----------------
    spi_bus_config_t buscfg = {
        .miso_io_num = W5500_MISO_GPIO,
        .mosi_io_num = W5500_MOSI_GPIO,
        .sclk_io_num = W5500_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    err = spi_bus_initialize(W5500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return;
    }

    // ----------------- SPI device config (kept alive) -----------------
    // Some IDF variants expect a pointer to spi_device_interface_config_t inside eth_w5500_config_t (spi_devcfg). Create a static instance so the pointer remains valid for the lifetime of the driver.
    static spi_device_interface_config_t w5500_spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000, /* 20 MHz */
        .spics_io_num = W5500_CS_GPIO,
        .queue_size = 20,
    };

    /* ----------------- MAC / PHY config ----------------- */
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    /* If your PHY requires a reset GPIO, set it here (some variants use reset_gpio_num) */
#ifdef W5500_RST_GPIO
    phy_config.reset_gpio_num = W5500_RST_GPIO;
#endif
    phy_config.phy_addr = 0;

    // ----------------- W5500 driver config -----------------
    //   Zero-init the struct and set the fields your local IDF expects. Common fields: spi_host_id, spi_devcfg (pointer), int_gpio_num, reset_gpio_num. If your local eth_w5500_config_t uses different member names, adjust accordingly.
    eth_w5500_config_t w5500_cfg;
    memset(&w5500_cfg, 0, sizeof(w5500_cfg));

    // SPI host id (most IDF variants use spi_host_id)
    w5500_cfg.spi_host_id = W5500_SPI_HOST;

    // Provide pointer to the spi_device_interface_config_t we created above. Some IDF variants name this member spi_devcfg or spi_dev_config; use the exact member name from your local header.
    w5500_cfg.spi_devcfg = &w5500_spi_devcfg;

    // Optional interrupt GPIO
#ifdef W5500_INT_GPIO
    if (W5500_INT_GPIO >= 0)
    {
        w5500_cfg.int_gpio_num = W5500_INT_GPIO;
    }
#endif

    /* Optional reset GPIO (if present in the struct) */
    // #ifdef W5500_RST_GPIO
    //     if (W5500_RST_GPIO >= 0)
    //{
    /* Some headers expect reset_gpio_num in the w5500 config as well */
    //        w5500_cfg.reset_gpio_num = W5500_RST_GPIO;
    //}
    // #endif

    /* Debug: log what we're passing to the driver */
    //    LOGD(TAG, "W5500 config: spi_host_id=%d spics=%d int=%d",
    ///        w5500_cfg.spi_host_id,
    //       w5500_spi_devcfg.spics_io_num,
    //       w5500_cfg.int_gpio_num;
    // w5500_cfg.reset_gpio_num);

    // ----------------- Create MAC and PHY instances -----------------
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_config);
    if (mac == NULL)
    {
        LOGE(TAG, "esp_eth_mac_new_w5500 returned NULL — check W5500 config and GPIO defines");
        return;
    }

    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (phy == NULL)
    {
        LOGE(TAG, "esp_eth_phy_new_w5500 returned NULL");
        return;
    }

    // ----------------- Install Ethernet driver -----------------
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&eth_config, &eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "esp_eth_driver_install failed: %s", esp_err_to_name(err));
        return;
    }

    // ----------------- Attach driver to netif -----------------
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
    if (glue == NULL)
    {
        LOGE(TAG, "esp_eth_new_netif_glue returned NULL");
        return;
    }

    err = esp_netif_attach(eth_netif, glue);
    if (err != ESP_OK)
    {
        LOGE(TAG, "esp_netif_attach failed: %s", esp_err_to_name(err));
        return;
    }

    LOGI(TAG, "W5500 initialized and attached to netif");
}

void eth_start(void)
{
    if (eth_handle == NULL)
    {
        LOGE(TAG, "eth_start: eth_handle is NULL, call eth_init() first");
        return;
    }
    esp_err_t err = esp_eth_start(eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "esp_eth_start failed: %s", esp_err_to_name(err));
    }
    else
    {
        LOGI(TAG, "Ethernet driver started");
    }
}

void eth_stop(void)
{
    if (eth_handle == NULL)
    {
        LOGW(TAG, "eth_stop: eth_handle is NULL");
        return;
    }
    esp_err_t err = esp_eth_stop(eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "esp_eth_stop failed: %s", esp_err_to_name(err));
    }
    else
    {
        LOGI(TAG, "Ethernet driver stopped");
    }
}

// ---------- Config helpers (read config under mutex) ----------
static bool cfg_str_is_on(const char *s)
{
    return (s && strcmp(s, "On") == 0);
}

bool ethernet_config_is_enabled(void)
{
    bool enabled = false;
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        enabled = cfg_str_is_on(ethernet_config.enable_ethernet);
        xSemaphoreGive(network_config_mutex);
    }
    else
    {
        LOGW(TAG, "Could not take network_config_mutex; assuming ethernet disabled");
    }
    return enabled;
}

bool ethernet_config_is_dhcp_enabled(void)
{
    bool dhcp = true;
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        dhcp = cfg_str_is_on(ethernet_config.enable_dhcp);
        xSemaphoreGive(network_config_mutex);
    }
    else
    {
        LOGW(TAG, "Could not take network_config_mutex; assuming DHCP enabled");
    }
    return dhcp;
}

// Helper to check if Ethernet is currently connected (based on event handler updates)
bool eth_is_connected(void)
{
    return eth_connected; // updated in event handler
}

/* ---------- Apply Ethernet configuration (DHCP/static) ---------- */
static bool validate_ipv4_string(const char *s)
{
    if (!s || s[0] == '\0')
        return false;
    struct in_addr a;
    return inet_pton(AF_INET, s, &a) == 1;
}

esp_err_t ethernet_config_apply(void)
{
    if (!eth_netif)
    {
        LOGW(TAG, "eth_netif is NULL; call eth_init() before applying config");
        return ESP_ERR_INVALID_STATE;
    }

    Ethernet_config_t local;
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        LOGI(TAG, "enable_ethernet: %s, enable_dhcp: %s", ethernet_config.enable_ethernet, ethernet_config.enable_dhcp);
        memcpy(&local, &ethernet_config, sizeof(Ethernet_config_t));
        xSemaphoreGive(network_config_mutex);
    }
    else
    {
        LOGW(TAG, "Could not take network_config_mutex; aborting ethernet_config_apply");
        return ESP_ERR_TIMEOUT;
    }

    if (strcmp(local.enable_ethernet, "On") != 0)
    {
        LOGI(TAG, "Ethernet disabled in config; stopping driver");
        eth_stop();
        return ESP_OK;
    }

    LOGI(TAG, "Ethernet enabled in config; starting driver");
    // eth_start();

    if (strcmp(local.enable_dhcp, "On") == 0)
    {
        LOGI(TAG, "Starting DHCP client on Ethernet");
        esp_err_t err = esp_netif_dhcpc_start(eth_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
        {
            LOGE(TAG, "esp_netif_dhcpc_start failed: %s", esp_err_to_name(err));
            return err;
        }
        return ESP_OK;
    }

    /* Static IP path */
    if (!validate_ipv4_string(local.ip_address) ||
        !validate_ipv4_string(local.subnet_mask) ||
        !validate_ipv4_string(local.gateway) ||
        !validate_ipv4_string(local.primary_dns))
    {
        LOGE(TAG, "Invalid static IP configuration; not applying");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_netif_dhcpc_stop(eth_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
    {
        LOGW(TAG, "esp_netif_dhcpc_stop returned %s", esp_err_to_name(err));
    }

    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    if (inet_pton(AF_INET, local.ip_address, &ip_info.ip) != 1 ||
        inet_pton(AF_INET, local.subnet_mask, &ip_info.netmask) != 1 ||
        inet_pton(AF_INET, local.gateway, &ip_info.gw) != 1)
    {
        LOGE(TAG, "inet_pton failed for static IP fields");
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_netif_set_ip_info(eth_netif, &ip_info) != ESP_OK)
    {
        LOGE(TAG, "esp_netif_set_ip_info failed");
        return ESP_FAIL;
    }

    esp_netif_dns_info_t dns;
    memset(&dns, 0, sizeof(dns));
    if (inet_pton(AF_INET, local.primary_dns, &dns.ip.u_addr.ip4) == 1)
    {
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(eth_netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    else
    {
        LOGW(TAG, "Primary DNS invalid; skipping");
    }

    if (strlen(local.secondary_dns) > 0)
    {
        memset(&dns, 0, sizeof(dns));
        if (inet_pton(AF_INET, local.secondary_dns, &dns.ip.u_addr.ip4) == 1)
        {
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(eth_netif, ESP_NETIF_DNS_BACKUP, &dns);
        }
        else
        {
            LOGW(TAG, "Secondary DNS invalid; skipping");
        }
    }

    LOGI(TAG, "Static IP applied: %s", local.ip_address);
    return ESP_OK;
}

// ---------- Event handlers and manager ----------
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
        LOGI(TAG, "ETH_EVENT: connected");
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
        {
            sm_actuals.eth_connected = true;
            xSemaphoreGive(statemachine_mutex);
        }
        break;

    case ETHERNET_EVENT_DISCONNECTED:
        LOGW(TAG, "ETH_EVENT: disconnected");
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
        {
            sm_actuals.eth_connected = false;
            xSemaphoreGive(statemachine_mutex);
        }
        break;

    case ETHERNET_EVENT_START:
        LOGI(TAG, "ETH_EVENT: start");
        break;

    case ETHERNET_EVENT_STOP:
        LOGI(TAG, "ETH_EVENT: stop");
        eth_connected = false;
        break;

    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        LOGI(TAG, "ETH got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void eth_manager_start(void)
{
    LOGI(TAG, "Starting Ethernet manager...");

    // 1. Initialize hardware + netif + driver
    eth_init();

    // 2. Register event handlers BEFORE starting driver
    ESP_ERROR_CHECK(esp_event_handler_register(
        ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    // 3. Start Ethernet driver ONCE
    LOGI(TAG, "Starting Ethernet driver...");
    eth_start();

    // 4. Apply DHCP/static configuration
    ethernet_config_apply();

    // 5. Do NOT check link status here — link events are asynchronous and will be logged by the event handlers. The manager should remain running to handle these events and apply config changes on the fly.
    LOGI(TAG, "Ethernet manager initialized; waiting for link events.");
}
