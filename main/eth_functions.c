/*
    eth_functions.c
    Ethernet driver + config helpers only.

    All events and LEDs are handled in network_manager.c.
*/

#include "eth_functions.h"

static const char *TAG = "eth_func";

esp_netif_t *eth_netif = NULL;
esp_eth_handle_t eth_handle = NULL;

static bool eth_started = false;
static bool s_eth_initialized = false;

static SemaphoreHandle_t eth_lifecycle_mutex = NULL;
static esp_eth_netif_glue_handle_t eth_glue = NULL;

// Call once early in startup (after RTOS running)
void eth_lifecycle_init_once(void)
{
    if (eth_lifecycle_mutex == NULL)
    {
        eth_lifecycle_mutex = xSemaphoreCreateMutex();
        if (eth_lifecycle_mutex == NULL)
        {
            LOGE(TAG, "eth_lifecycle_init_once: failed to create mutex");
        }
    }
}

// Internal helper: fully teardown ethernet resources
void eth_teardown_safe(void)
{
    eth_lifecycle_init_once();

    if (!hw.has_ethernet)
    {
        LOGI(TAG, "eth_teardown_safe: no ethernet on this hardware");
        return;
    }

    if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        LOGE(TAG, "eth_teardown_safe: failed to take lifecycle mutex");
        return;
    }

    LOGI(TAG, "eth_teardown_safe: begin (eth_handle=%p eth_netif=%p eth_glue=%p)",
         (void *)eth_handle, (void *)eth_netif, (void *)eth_glue);

    /* 1) Stop DHCP client on the netif (best effort) */
    if (eth_netif)
    {
        esp_err_t r = esp_netif_dhcpc_stop(eth_netif);
        if (r != ESP_OK && r != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
        {
            LOGW(TAG, "dhcpc_stop -> %s", esp_err_to_name(r));
        }
    }

    /* 2) Stop the Ethernet driver if started */
    if (eth_handle && eth_started)
    {
        esp_err_t r = esp_eth_stop(eth_handle);
        if (r != ESP_OK)
            LOGW(TAG, "esp_eth_stop -> %s", esp_err_to_name(r));
        else
            LOGI(TAG, "esp_eth_stop succeeded");
        eth_started = false;
    }

    /* 3) Delete glue if present (this will detach glue from netif internally) */
    if (eth_glue)
    {
        esp_err_t r = esp_eth_del_netif_glue(eth_glue);
        if (r != ESP_OK)
            LOGW(TAG, "esp_eth_del_netif_glue -> %s", esp_err_to_name(r));
        else
            LOGI(TAG, "esp_eth_del_netif_glue succeeded");
        eth_glue = NULL;
    }
    else
    {
        LOGD(TAG, "no eth_glue to delete");
    }

    /* 4) Uninstall driver (frees driver resources) */
    if (eth_handle)
    {
        esp_err_t r = esp_eth_driver_uninstall(eth_handle);
        if (r != ESP_OK)
            LOGW(TAG, "esp_eth_driver_uninstall -> %s", esp_err_to_name(r));
        else
            LOGI(TAG, "esp_eth_driver_uninstall succeeded");
        eth_handle = NULL;
    }
    else
    {
        LOGD(TAG, "no eth_handle to uninstall");
    }

    /* 5) Clear netif pointer and flags */
    eth_netif = NULL;
    s_eth_initialized = false;
    eth_started = false;

    LOGI(TAG, "eth_teardown_safe: complete");
    xSemaphoreGive(eth_lifecycle_mutex);
}

bool eth_is_connected(void)
{
    bool connected = false;

    if (statemachine_mutex &&
        xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        connected = sm_actuals.eth_connected;
        xSemaphoreGive(statemachine_mutex);
    }

    return connected;
}

static bool cfg_str_is_on(const char *s)
{
    return (s && strcmp(s, "On") == 0);
}

// Safe eth_init: idempotent, returns ESP_OK on success or existing init
void eth_init(void)
{
    if (!hw.has_ethernet)
    {
        LOGI(TAG, "eth_init: Ethernet not present on this hardware");
        return;
    }

    if (eth_lifecycle_mutex == NULL)
        eth_lifecycle_init_once();

    if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        LOGE(TAG, "eth_init: failed to take lifecycle mutex");
        return;
    }

    if (s_eth_initialized && eth_handle != NULL)
    {
        LOGD(TAG, "eth_init: already initialized");
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    LOGI(TAG, "eth_init: initializing W5500 Ethernet (safe)");

    // Ensure TCP/IP stack initialized (idempotent)
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "eth_init: esp_netif_init failed: %s", esp_err_to_name(err));
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    // Reuse existing netif if present, otherwise create
    esp_netif_t *existing = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (existing != NULL)
    {
        eth_netif = existing;
        LOGI(TAG, "eth_init: reusing existing ETH_DEF netif handle %p", (void *)eth_netif);
    }
    else
    {
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        eth_netif = esp_netif_new(&cfg);
        if (eth_netif == NULL)
        {
            LOGE(TAG, "eth_init: esp_netif_new returned NULL");
            xSemaphoreGive(eth_lifecycle_mutex);
            return;
        }
        LOGI(TAG, "eth_init: created new ETH_DEF netif %p", (void *)eth_netif);
    }

    // SPI bus init (idempotent)
    spi_bus_config_t buscfg = {
        .miso_io_num = hw.w5500_miso_gpio,
        .mosi_io_num = hw.w5500_mosi_gpio,
        .sclk_io_num = hw.w5500_sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    err = spi_bus_initialize(W5500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "eth_init: spi_bus_initialize failed: %s", esp_err_to_name(err));
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    // SPI device config
    static spi_device_interface_config_t w5500_spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = -1,
        .queue_size = 20,
    };
    w5500_spi_devcfg.spics_io_num = hw.w5500_cs_gpio;

    // MAC + PHY
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    if (hw.w5500_rst_gpio >= 0)
        phy_config.reset_gpio_num = hw.w5500_rst_gpio;
    phy_config.phy_addr = 0;

    eth_w5500_config_t w5500_cfg = {0};
    w5500_cfg.spi_host_id = W5500_SPI_HOST;
    w5500_cfg.spi_devcfg = &w5500_spi_devcfg;
    if (hw.w5500_int_gpio >= 0)
        w5500_cfg.int_gpio_num = hw.w5500_int_gpio;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_config);
    if (mac == NULL)
    {
        LOGE(TAG, "eth_init: esp_eth_mac_new_w5500 returned NULL");
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (phy == NULL)
    {
        LOGE(TAG, "eth_init: esp_eth_phy_new_w5500 returned NULL");
        // free mac if needed (IDF provides free functions in some versions)
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    // Install driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&eth_config, &eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "eth_init: esp_eth_driver_install failed: %s", esp_err_to_name(err));
        eth_handle = NULL;
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    // Attach netif glue
    eth_glue = esp_eth_new_netif_glue(eth_handle);
    if (eth_glue == NULL)
    {
        LOGE(TAG, "esp_eth_new_netif_glue returned NULL");
        esp_eth_driver_uninstall(eth_handle);
        eth_handle = NULL;
        return;
    }
    err = esp_netif_attach(eth_netif, eth_glue);
    if (err != ESP_OK)
    {
        LOGE(TAG, "esp_netif_attach failed: %s", esp_err_to_name(err));
        esp_eth_del_netif_glue(eth_glue);
        eth_glue = NULL;
        esp_eth_driver_uninstall(eth_handle);
        eth_handle = NULL;
        return;
    }

    LOGI(TAG, "eth_init: W5500 initialized and attached to netif");
    s_eth_initialized = true;
    eth_started = false; // not started yet

    xSemaphoreGive(eth_lifecycle_mutex);
}

/*void eth_init(void)
{
    if (!hw.has_ethernet)
    {
        LOGI(TAG, "eth_init: Ethernet not present on this hardware");
        return;
    }

    if (s_eth_initialized)
    {
        LOGD(TAG, "eth_init: already initialized");
        if (eth_netif == NULL)
        {
            esp_netif_t *existing = esp_netif_get_handle_from_ifkey("ETH_DEF");
            if (existing)
            {
                eth_netif = existing;
                LOGI(TAG, "eth_init: recovered existing ETH_DEF netif handle %p", (void *)eth_netif);
            }
        }
        return;
    }

    LOGI(TAG, "eth_init: initializing W5500 Ethernet");

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return;
    }

    // Reuse existing netif if present
    esp_netif_t *existing = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (existing != NULL)
    {
        LOGI(TAG, "eth_init: ETH_DEF netif already exists, reusing handle %p", (void *)existing);
        eth_netif = existing;
    }
    else
    {
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        eth_netif = esp_netif_new(&cfg);
        if (eth_netif == NULL)
        {
            LOGE(TAG, "esp_netif_new(ESP_NETIF_DEFAULT_ETH) returned NULL");
            return;
        }
    }

    // --- SPI BUS INIT ---
    spi_bus_config_t buscfg = {
        .miso_io_num = hw.w5500_miso_gpio,
        .mosi_io_num = hw.w5500_mosi_gpio,
        .sclk_io_num = hw.w5500_sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    err = spi_bus_initialize(W5500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return;
    }

    // SPI device config
    static spi_device_interface_config_t w5500_spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = -1,
        .queue_size = 20,
    };

    // Runtime assignment
    w5500_spi_devcfg.spics_io_num = hw.w5500_cs_gpio;

    // --- MAC + PHY CONFIG ---
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    if (hw.w5500_rst_gpio >= 0)
        phy_config.reset_gpio_num = hw.w5500_rst_gpio;

    phy_config.phy_addr = 0;

    // --- W5500 CONFIG ---
    eth_w5500_config_t w5500_cfg = {0};
    w5500_cfg.spi_host_id = W5500_SPI_HOST;
    w5500_cfg.spi_devcfg = &w5500_spi_devcfg;

    if (hw.w5500_int_gpio >= 0)
        w5500_cfg.int_gpio_num = hw.w5500_int_gpio;

    // --- CREATE MAC ---
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_config);
    if (mac == NULL)
    {
        LOGE(TAG, "esp_eth_mac_new_w5500 returned NULL — check W5500 config and GPIO pins");
        return;
    }

    // --- CREATE PHY ---
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (phy == NULL)
    {
        LOGE(TAG, "esp_eth_phy_new_w5500 returned NULL");
        return;
    }

    // --- INSTALL DRIVER ---
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&eth_config, &eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "esp_eth_driver_install failed: %s", esp_err_to_name(err));
        return;
    }

    // --- ATTACH NETIF ---
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

    s_eth_initialized = true;
}*/

/*void eth_start(void)
{
    if (eth_handle == NULL)
    {
        LOGE(TAG, "eth_start: eth_handle is NULL, call eth_init() first");
        return;
    }
    if (eth_started)
    {
        LOGD(TAG, "Ethernet already started");
        return;
    }
    esp_err_t err = esp_eth_start(eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "esp_eth_start failed: %s", esp_err_to_name(err));
    }
    else
    {
        eth_started = true;
        LOGI(TAG, "Ethernet driver started");
    }
}*/

// Safe eth_start: idempotent and mutex-protected
void eth_start(void)
{
    if (!hw.has_ethernet)
    {
        LOGI(TAG, "eth_start: Ethernet not present");
        return;
    }

    if (eth_lifecycle_mutex == NULL)
        eth_lifecycle_init_once();

    if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        LOGE(TAG, "eth_start: failed to take lifecycle mutex");
        return;
    }

    if (!s_eth_initialized || eth_handle == NULL)
    {
        LOGW(TAG, "eth_start: not initialized; calling eth_init()");
        eth_init();
        // eth_init will release the mutex; reacquire to continue
        if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
        {
            LOGE(TAG, "eth_start: failed to re-take lifecycle mutex after init");
            return;
        }
    }

    if (eth_started)
    {
        LOGD(TAG, "eth_start: already started");
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    esp_err_t err = esp_eth_start(eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "eth_start: esp_eth_start failed: %s", esp_err_to_name(err));
    }
    else
    {
        eth_started = true;
        LOGI(TAG, "eth_start: Ethernet driver started");
    }

    xSemaphoreGive(eth_lifecycle_mutex);
}

/*void eth_stop(void)
{
    if (eth_handle == NULL && hw.has_ethernet)
    {
        LOGW(TAG, "eth_stop: eth_handle is NULL");
        return;
    }
    if (!eth_started)
    {
        LOGI(TAG, "Ethernet already stopped");
        return;
    }
    esp_err_t err = esp_eth_stop(eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "esp_eth_stop failed: %s", esp_err_to_name(err));
    }
    else
    {
        eth_started = false;
        LOGI(TAG, "Ethernet driver stopped");
    }
}*/

// Safe eth_stop: idempotent and performs full cleanup
void eth_stop(void)
{
    if (!hw.has_ethernet)
    {
        LOGD(TAG, "Ethernet not present");
        return;
    }

    if (eth_lifecycle_mutex == NULL)
        eth_lifecycle_init_once();

    if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        LOGE(TAG, "eth_stop: failed to take lifecycle mutex");
        return;
    }

    if (!s_eth_initialized && eth_handle == NULL && eth_netif == NULL)
    {
        LOGD(TAG, "eth_stop: already stopped/uninitialized");
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    LOGI(TAG, "eth_stop: tearing down Ethernet safely");

    // Full teardown
    eth_teardown_safe();

    LOGI(TAG, "eth_stop: teardown complete");

    xSemaphoreGive(eth_lifecycle_mutex);
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

static bool validate_ipv4_string(const char *s)
{
    if (!s || s[0] == '\0')
        return false;
    struct in_addr a;
    return inet_pton(AF_INET, s, &a) == 1;
}

esp_err_t ethernet_config_apply(void)
{
    char hostname[32] = {0};

    if (!eth_netif && hw.has_ethernet)
    {
        LOGW(TAG, "eth_netif is NULL; call eth_init() before applying config");
        return ESP_ERR_INVALID_STATE;
    }

    Ethernet_config_t local;
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        LOGD(TAG, "enable_ethernet: %s, enable_dhcp: %s", ethernet_config.enable_ethernet, ethernet_config.enable_dhcp);
        memcpy(&local, &ethernet_config, sizeof(Ethernet_config_t));
        strncpy(hostname, wifi_config.hostname, sizeof(hostname) - 1);
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

    LOGD(TAG, "Setting Ethernet hostname to: %s", hostname);
    esp_err_t herr = esp_netif_set_hostname(eth_netif, hostname);
    if (herr != ESP_OK)
    {
        LOGW(TAG, "Failed to set Ethernet hostname: %s", esp_err_to_name(herr));
    }

    LOGD(TAG, "Ethernet enabled in config; starting driver if needed");
    eth_start();

    if (strcmp(local.enable_dhcp, "On") == 0)
    {
        LOGD(TAG, "Starting DHCP client on Ethernet");
        esp_err_t err = esp_netif_dhcpc_start(eth_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
        {
            LOGE(TAG, "esp_netif_dhcpc_start failed: %s", esp_err_to_name(err));
            return err;
        }
        return ESP_OK;
    }

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
