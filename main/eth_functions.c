/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "eth_functions.h"

static const char *TAG = "eth_func";

esp_netif_t *eth_netif = NULL;
esp_eth_handle_t eth_handle = NULL;

static bool eth_started = false;
static bool s_eth_initialized = false;
static volatile bool s_eth_shutting_down = false;

static volatile bool s_deferred_eth_teardown = false;
static int s_eth_restart_attempts = 0;
static const int ETH_RESTART_MAX_ATTEMPTS = 3;
static const TickType_t ETH_RESTART_BACKOFF_TICKS = pdMS_TO_TICKS(5000);

static SemaphoreHandle_t eth_lifecycle_mutex = NULL;
static esp_eth_netif_glue_handle_t eth_glue = NULL;

static void eth_teardown_locked(void)
{
    /* Caller must hold eth_lifecycle_mutex */
    /* Clear deferred flag and mark shutting down */
    s_deferred_eth_teardown = false;
    eth_set_shutting_down(true);

    /* Stop DHCP client (best effort) */
    if (eth_netif)
    {
        esp_err_t r = esp_netif_dhcpc_stop(eth_netif);
        if (r != ESP_OK && r != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
            LOGW(TAG, "dhcpc_stop -> %s", esp_err_to_name(r));
    }

    /* Stop driver if running */
    if (eth_handle && eth_started)
    {
        esp_err_t r = esp_eth_stop(eth_handle);
        if (r != ESP_OK)
            LOGW(TAG, "esp_eth_stop -> %s", esp_err_to_name(r));
        eth_started = false;
    }

    /* Defensive: remove any GPIO ISR for W5500 interrupt pin (if used) */
    if (hw.w5500_int_gpio >= 0)
    {
        esp_err_t r = gpio_isr_handler_remove((gpio_num_t)hw.w5500_int_gpio);
        if (r != ESP_OK && r != ESP_ERR_INVALID_ARG)
            LOGW(TAG, "gpio_isr_handler_remove(w5500_int) -> %s", esp_err_to_name(r));
    }

    /* Delete glue if present */
    if (eth_glue)
    {
        esp_err_t r = esp_eth_del_netif_glue(eth_glue);
        if (r != ESP_OK)
            LOGW(TAG, "esp_eth_del_netif_glue -> %s", esp_err_to_name(r));
        eth_glue = NULL;
    }

    /* Uninstall driver */
    if (eth_handle)
    {
        esp_err_t r = esp_eth_driver_uninstall(eth_handle);
        if (r != ESP_OK)
            LOGW(TAG, "esp_eth_driver_uninstall -> %s", esp_err_to_name(r));
        eth_handle = NULL;
    }

    /* Clear netif pointer and flags */
    eth_netif = NULL;
    s_eth_initialized = false;
    eth_started = false;

    LOGI(TAG, "eth_teardown_locked: complete");

    /* Clear shutting down flag (caller will release mutex) */
    eth_set_shutting_down(false);
}

// ---------------------------------------------------------------------------
// Hardware reset helper for W5500
// ---------------------------------------------------------------------------

esp_err_t eth_hardware_reset(void)
{
    // Ensure lifecycle mutex exists
    eth_lifecycle_init_once();

    // Try to take lifecycle mutex; if busy, defer and return busy
    if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
    {
        LOGW(TAG, "eth_hardware_reset: lifecycle mutex busy; cannot perform hardware reset now");
        return ESP_ERR_TIMEOUT;
    }

    LOGI(TAG, "eth_hardware_reset: starting guarded hardware reset");

    // Mark shutting down to prevent event handlers from racing
    eth_set_shutting_down(true);

    // Stop DHCP client (best effort)
    if (eth_netif)
    {
        esp_err_t r = esp_netif_dhcpc_stop(eth_netif);
        if (r != ESP_OK && r != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
            LOGW(TAG, "eth_hardware_reset: dhcpc_stop -> %s", esp_err_to_name(r));
    }

    // Stop driver if running
    if (eth_handle && eth_started)
    {
        esp_err_t r = esp_eth_stop(eth_handle);
        if (r != ESP_OK)
            LOGW(TAG, "eth_hardware_reset: esp_eth_stop -> %s", esp_err_to_name(r));
        eth_started = false;
    }

    // Optionally toggle reset GPIO if configured
    if (hw.w5500_rst_gpio >= 0)
    {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << hw.w5500_rst_gpio),
            .pull_down_en = 0,
            .pull_up_en = 0,
            .intr_type = GPIO_INTR_DISABLE};
        gpio_config(&io_conf);

        LOGI(TAG, "eth_hardware_reset: asserting W5500 reset GPIO %d", hw.w5500_rst_gpio);
        gpio_set_level((gpio_num_t)hw.w5500_rst_gpio, 0); // assert reset (active low)
        vTaskDelay(pdMS_TO_TICKS(100));                   // hold reset (100 ms recommended)

        LOGI(TAG, "eth_hardware_reset: releasing W5500 reset GPIO %d", hw.w5500_rst_gpio);
        gpio_set_level((gpio_num_t)hw.w5500_rst_gpio, 1); // release reset
        vTaskDelay(pdMS_TO_TICKS(300));                   // allow W5500 to boot (300 ms)
    }
    else
    {
        LOGW(TAG, "eth_hardware_reset: no reset GPIO configured; performing software reinit only");
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Reinitialize driver and netif if needed.
    if (!s_eth_initialized || eth_handle == NULL || eth_netif == NULL)
    {
        LOGI(TAG, "eth_hardware_reset: calling eth_init()");
        eth_init();
        // eth_init releases the mutex; reacquire to continue if needed
        if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
        {
            LOGW(TAG, "eth_hardware_reset: failed to re-take lifecycle mutex after eth_init");
            eth_set_shutting_down(false);
            xSemaphoreGive(eth_lifecycle_mutex);
            return ESP_ERR_TIMEOUT;
        }
    }

    // Start driver if not started
    if (!eth_started && eth_handle != NULL)
    {
        esp_err_t r = esp_eth_start(eth_handle);
        if (r != ESP_OK)
        {
            LOGW(TAG, "eth_hardware_reset: esp_eth_start -> %s", esp_err_to_name(r));
        }
        else
        {
            eth_started = true;
            LOGI(TAG, "eth_hardware_reset: esp_eth_start succeeded");
        }
    }

    // Clear shutting down flag and release mutex
    eth_set_shutting_down(false);
    xSemaphoreGive(eth_lifecycle_mutex);

    LOGI(TAG, "eth_hardware_reset: complete");
    return ESP_OK;
}

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

    /* Try to take lifecycle mutex briefly; if busy, mark deferred and return */
    if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
    {
        LOGW(TAG, "eth_teardown_safe: lifecycle mutex busy; deferring teardown");
        s_deferred_eth_teardown = true;
        return;
    }

    /* We hold the mutex now; perform locked teardown */
    eth_teardown_locked();

    /* Release mutex */
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

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "eth_init: esp_netif_init failed: %s", esp_err_to_name(err));
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    /* Reuse or create ETH_DEF netif */
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

    /* SPI bus init (idempotent) */
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

    /* SPI device config */
    static spi_device_interface_config_t w5500_spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = -1,
        .queue_size = 20,
    };
    w5500_spi_devcfg.spics_io_num = hw.w5500_cs_gpio;

    /* MAC + PHY */
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
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    /* Install driver */
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&eth_config, &eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "eth_init: esp_eth_driver_install failed: %s", esp_err_to_name(err));
        eth_handle = NULL;
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    /* Attach netif glue */
    eth_glue = esp_eth_new_netif_glue(eth_handle);
    if (eth_glue == NULL)
    {
        LOGE(TAG, "eth_init: eth_glue NULL, aborting");
        if (eth_handle)
        {
            esp_eth_driver_uninstall(eth_handle);
            eth_handle = NULL;
        }
        xSemaphoreGive(eth_lifecycle_mutex);
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
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    LOGI(TAG, "eth_init: W5500 initialized and attached to netif");
    s_eth_initialized = true;
    eth_started = false;

    /* Start DHCP client if configured for DHCP */
    if (ethernet_config_is_dhcp_enabled())
    {
        esp_err_t d = esp_netif_dhcpc_start(eth_netif);
        if (d != ESP_OK && d != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
            LOGW(TAG, "eth_init: esp_netif_dhcpc_start failed: %s", esp_err_to_name(d));
        else
            LOGI(TAG, "eth_init: DHCP client started on ETH_DEF");
    }

    xSemaphoreGive(eth_lifecycle_mutex);
}

bool ethernet_link_is_up(void)
{
#if defined(ETH_CMD_G_PHY_LINK)
    if (eth_handle != NULL)
    {
        int link = 0;
        esp_err_t r = esp_eth_ioctl(eth_handle, ETH_CMD_G_PHY_LINK, &link);
        if (r == ESP_OK)
        {
            return (link != 0);
        }
        // fall through to netif/IP fallback on ioctl failure
    }
#endif

    if (eth_netif == NULL)
        return false;

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(eth_netif, &ip) != ESP_OK)
        return false;

    return ip.ip.addr != 0;
}

// Attempt a guarded restart: only restart if link is down, apply backoff and defer teardown if busy.
void attempt_guarded_eth_restart(void)
{
    /* If link/netif appears up, skip restart (DHCP may still be negotiating) */
    if (ethernet_link_is_up())
    {
        LOGI(TAG, "attempt_guarded_eth_restart: link/netif up; skipping restart");
        s_eth_restart_attempts = 0;
        return;
    }

    /* Backoff limit to avoid restart storms */
    if (s_eth_restart_attempts >= ETH_RESTART_MAX_ATTEMPTS)
    {
        LOGW(TAG, "attempt_guarded_eth_restart: max attempts reached; delaying longer");
        vTaskDelay(pdMS_TO_TICKS(60000));
        s_eth_restart_attempts = 0;
        return;
    }

    s_eth_restart_attempts++;
    LOGI(TAG, "attempt_guarded_eth_restart: attempt %d/%d", s_eth_restart_attempts, ETH_RESTART_MAX_ATTEMPTS);

    /* Try teardown; eth_teardown_safe() will set s_deferred_eth_teardown if it cannot take the mutex */
    eth_teardown_safe();

    if (s_deferred_eth_teardown)
    {
        LOGW(TAG, "attempt_guarded_eth_restart: teardown deferred; will retry later");
        return;
    }

    /* Small settle delay */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Reinitialize and start */
    eth_init();
    vTaskDelay(pdMS_TO_TICKS(200));
    eth_start();

    /* Allow link/IP to settle a bit */
    vTaskDelay(ETH_RESTART_BACKOFF_TICKS);
}

// Called periodically from network_manager_task to retry deferred teardown
void handle_deferred_eth_teardown_if_needed(void)
{
    if (!s_deferred_eth_teardown)
        return;

    LOGI(TAG, "handle_deferred_eth_teardown_if_needed: attempting deferred teardown");

    if (eth_lifecycle_mutex == NULL)
        eth_lifecycle_init_once();

    if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
    {
        LOGW(TAG, "handle_deferred_eth_teardown_if_needed: lifecycle mutex still busy");
        return;
    }

    /* Clear deferred flag and perform locked teardown */
    s_deferred_eth_teardown = false;
    eth_teardown_locked();

    LOGI(TAG, "handle_deferred_eth_teardown_if_needed: deferred teardown complete");

    xSemaphoreGive(eth_lifecycle_mutex);
}

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

    if (eth_handle == NULL)
    {
        LOGW(TAG, "eth_start: eth_handle NULL, cannot start");
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

    /* We already hold the mutex, call locked teardown directly */
    eth_teardown_locked();

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

    // esp_err_t err = esp_netif_dhcpc_stop(eth_netif);
    // if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
    //{
    //  LOGW(TAG, "esp_netif_dhcpc_stop returned %s", esp_err_to_name(err));
    //}

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

// Mark that ethernet is being torn down (or cleared). Use accessor functionsso other modules don't reference the static variable directly.
void eth_set_shutting_down(bool v)
{
    s_eth_shutting_down = v;
}

bool eth_is_shutting_down(void)
{
    return s_eth_shutting_down;
}

esp_eth_handle_t eth_get_handle(void)
{
    return eth_handle;
}
