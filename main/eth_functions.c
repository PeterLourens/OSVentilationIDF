/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file eth_functions.c
 * @brief Ethernet lifecycle and configuration support for W5500-based interfaces.
 *
 * This module manages safe initialization, teardown, restart, and configuration of
 * Ethernet hardware and the associated ESP-NETIF interface. It also provides helper
 * functions for link status checks and DHCP/static IP application.
 */

#include "eth_functions.h"
#include <esp_mac.h>

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

/**
 * @brief Teardown Ethernet resources while holding the lifecycle mutex.
 *
 * Caller must hold @c eth_lifecycle_mutex before invoking this helper.
 * It stops DHCP, stops and uninstalls the Ethernet driver, removes glue,
 * clears GPIO ISR handlers, and resets state flags.
 */
static void eth_teardown_locked(void)
{
    // Caller must hold eth_lifecycle_mutex
    // Clear deferred flag and mark shutting down
    s_deferred_eth_teardown = false;
    eth_set_shutting_down(true);

    // Stop DHCP client (best effort)
    if (eth_netif)
    {
        esp_err_t r = esp_netif_dhcpc_stop(eth_netif);
        if (r != ESP_OK && r != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
            LOGW(TAG, "dhcpc_stop -> %s", esp_err_to_name(r));
    }

    // Stop driver if running
    if (eth_handle && eth_started)
    {
        esp_err_t r = esp_eth_stop(eth_handle);
        if (r != ESP_OK)
            LOGW(TAG, "esp_eth_stop -> %s", esp_err_to_name(r));
        eth_started = false;
    }

    // Defensive: remove any GPIO ISR for W5500 interrupt pin (if used)
    if (hw.w5500_int_gpio >= 0)
    {
        esp_err_t r = gpio_isr_handler_remove((gpio_num_t)hw.w5500_int_gpio);
        if (r != ESP_OK && r != ESP_ERR_INVALID_ARG)
            LOGW(TAG, "gpio_isr_handler_remove(w5500_int) -> %s", esp_err_to_name(r));
    }

    // Delete glue if present
    if (eth_glue)
    {
        esp_err_t r = esp_eth_del_netif_glue(eth_glue);
        if (r != ESP_OK)
            LOGW(TAG, "esp_eth_del_netif_glue -> %s", esp_err_to_name(r));
        eth_glue = NULL;
    }

    if (eth_handle)
    {
        esp_err_t r = esp_eth_driver_uninstall(eth_handle);
        if (r != ESP_OK)
            LOGW(TAG, "esp_eth_driver_uninstall -> %s", esp_err_to_name(r));
        eth_handle = NULL;
    }

    // Destroy the netif only after uninstalling the driver. The W5500 receive
    // task is deleted by esp_eth_driver_uninstall and may still be finishing a
    // packet callback after esp_eth_stop returns.
    if (eth_netif)
    {
        esp_netif_destroy(eth_netif);
        eth_netif = NULL;
    }

    s_eth_initialized = false;
    eth_started = false;

    LOGI(TAG, "eth_teardown_locked: complete");

    // Clear shutting down flag (caller will release mutex)
    eth_set_shutting_down(false);
}

/**
 * @brief Check whether a configuration string is equal to "On".
 *
 * @param s The string to evaluate.
 * @return true when @p s is non-NULL and exactly "On", false otherwise.
 */
static bool cfg_str_is_on(const char *s)
{
    return (s && strcmp(s, "On") == 0);
}

/**
 * @brief Validate that a string contains a legal IPv4 address.
 *
 * @param s The null-terminated string to validate.
 * @return true if the string is a valid IPv4 address, false otherwise.
 */
static bool validate_ipv4_string(const char *s)
{
    if (!s || s[0] == '\0')
        return false;
    struct in_addr a;
    return inet_pton(AF_INET, s, &a) == 1;
}

/**
 * @brief Perform a guarded hardware reset of the W5500 Ethernet controller.
 *
 * This function acquires the Ethernet lifecycle mutex, stops DHCP and driver
 * activity, toggles the W5500 reset GPIO if configured, and reinitializes the
 * Ethernet stack. It ensures that no event handler races occur during reset.
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if the lifecycle mutex could not be taken,
 *         or an ESP error code from initialization or driver start operations.
 */
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
    LOGI(TAG, "eth_hardware_reset: DMA free=%u internal free=%u",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

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
        vTaskDelay(pdMS_TO_TICKS(200));                   // hold reset long enough for W5500 to enter reset reliably

        LOGI(TAG, "eth_hardware_reset: releasing W5500 reset GPIO %d", hw.w5500_rst_gpio);
        gpio_set_level((gpio_num_t)hw.w5500_rst_gpio, 1); // release reset
        vTaskDelay(pdMS_TO_TICKS(500));                   // allow W5500 to boot and settle before re-start
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
        LOGI(TAG, "eth_hardware_reset: before eth_init DMA free=%u internal free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

        // Release lifecycle mutex before calling eth_init() to avoid deadlock
        xSemaphoreGive(eth_lifecycle_mutex);
        eth_init();

        // Re-acquire lifecycle mutex to continue; handle failure
        if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
        {
            LOGW(TAG, "eth_hardware_reset: failed to re-take lifecycle mutex after eth_init");
            eth_set_shutting_down(false);
            return ESP_ERR_TIMEOUT;
        }
    }

    // Start driver if not started
    if (!eth_started && eth_handle != NULL)
    {
        LOGI(TAG, "eth_hardware_reset: before esp_eth_start DMA free=%u internal free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        esp_err_t r = esp_eth_start(eth_handle);
        if (r != ESP_OK)
        {
            LOGW(TAG, "eth_hardware_reset: esp_eth_start -> %s", esp_err_to_name(r));
        }
        else
        {
            eth_started = true;
            LOGI(TAG, "eth_hardware_reset: esp_eth_start succeeded");
            LOGI(TAG, "eth_hardware_reset: after esp_eth_start DMA free=%u internal free=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        }
    }

    // Clear shutting down flag and release mutex
    eth_set_shutting_down(false);
    xSemaphoreGive(eth_lifecycle_mutex);

    LOGI(TAG, "eth_hardware_reset: complete");
    return ESP_OK;
}

/**
 * @brief Safe reset helper: try software restart first, then guarded hardware reset.
 *
 * This wrapper prefers a light-weight software restart (eth_software_restart).
 * If that fails or does not restore link, it calls the guarded hardware reset
 * (eth_hardware_reset) which toggles the W5500 reset GPIO and reinitializes.
 *
 * @return ESP_OK on success, otherwise an esp_err_t describing the failure.
 */
esp_err_t eth_hardware_reset_safe(void)
{
    esp_err_t rc;

    // 1) Try software restart first (fast, non-invasive)
    LOGI(TAG, "eth_hardware_reset_safe: attempting software restart first");
    rc = eth_software_restart();
    if (rc == ESP_OK)
    {
        LOGI(TAG, "eth_hardware_reset_safe: eth_software_restart succeeded; allow settle and verify link");

        // Allow a short settle period and verify link/IP
        vTaskDelay(pdMS_TO_TICKS(500));

        // If link is up and IP present, consider success
        if (ethernet_link_is_up())
        {
            LOGI(TAG, "eth_hardware_reset_safe: link is up after software restart");
            return ESP_OK;
        }

        LOGW(TAG, "eth_hardware_reset_safe: link not up after software restart; will attempt hardware reset");
    }
    else
    {
        LOGW(TAG, "eth_hardware_reset_safe: eth_software_restart returned %s; will attempt hardware reset", esp_err_to_name(rc));
    }

    // 2) Fall back to guarded hardware reset (this function already handles lifecycle mutex)
    LOGI(TAG, "eth_hardware_reset_safe: attempting guarded hardware reset");
    rc = eth_hardware_reset();
    if (rc == ESP_OK)
    {
        // Allow PHY to boot and DHCP to start
        vTaskDelay(pdMS_TO_TICKS(500));
        LOGI(TAG, "eth_hardware_reset_safe: guarded hardware reset succeeded");
        return ESP_OK;
    }

    LOGE(TAG, "eth_hardware_reset_safe: guarded hardware reset failed: %s", esp_err_to_name(rc));
    return rc;
}

/**
 * @brief Initialize the Ethernet lifecycle mutex once.
 *
 * This function is safe to call repeatedly and creates the mutex the first time
 * it is needed.
 */
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

/**
 * @brief Safely teardown Ethernet resources, deferring if the lifecycle mutex is busy.
 *
 * If the mutex cannot be taken immediately, the teardown is deferred and a flag is set.
 */
void eth_teardown_safe(void)
{
    eth_lifecycle_init_once();

    // Try to take lifecycle mutex briefly; if busy, mark deferred and return
    if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
    {
        LOGW(TAG, "eth_teardown_safe: lifecycle mutex busy; deferring teardown");
        s_deferred_eth_teardown = true;
        return;
    }

    // We hold the mutex now; perform locked teardown
    eth_teardown_locked();

    // Release mutex
    xSemaphoreGive(eth_lifecycle_mutex);
}

/**
 * @brief Check whether the Ethernet interface is currently connected.
 *
 * This function verifies that Ethernet hardware is present, the netif exists,
 * the link is up, and a valid IPv4 address is assigned.
 *
 * @return true if Ethernet is connected and has an IP address, false otherwise.
 */
bool eth_is_connected(void)
{
    if (!hw.has_ethernet)
        return false;

    if (eth_is_shutting_down())
        return false;

    if (eth_handle == NULL || eth_netif == NULL)
        return false;

    if (!ethernet_link_is_up())
        return false;

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(eth_netif, &ip) != ESP_OK)
        return false;

    return ip.ip.addr != 0;
}

/**
 * @brief Initialize Ethernet hardware and attach the W5500 Ethernet driver.
 *
 * This function is idempotent and protects the Ethernet lifecycle using a mutex.
 * It sets up the SPI bus, MAC/PHY, driver, glue, and DHCP client if DHCP is enabled.
 */
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

    LOGI(TAG, "eth_init: entry DMA free=%u internal free=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "eth_init: esp_netif_init failed: %s", esp_err_to_name(err));
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    // Do not destroy a live ETH_DEF netif during normal reinitialization.
    // Reusing an old handle is much safer than tearing down a lwIP netif that
    // may still be referenced by event callbacks, the default route, or active
    // sockets. If the handle is already present, keep it stable and attach to it.
    esp_netif_t *existing = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (existing != NULL)
    {
        if (eth_netif == NULL)
        {
            eth_netif = existing;
            LOGW(TAG, "eth_init: reusing existing ETH_DEF netif %p; avoiding destructive reinit", (void *)eth_netif);
        }
        else if (eth_netif != existing)
        {
            LOGW(TAG, "eth_init: ETH_DEF netif already assigned to %p; leaving existing handle %p alone", (void *)eth_netif, (void *)existing);
        }
    }

    if (eth_netif == NULL)
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
    else
    {
        LOGI(TAG, "eth_init: using existing ETH_DEF netif %p", (void *)eth_netif);
    }

    // SPI bus init (idempotent)
    spi_bus_config_t buscfg = {
        .miso_io_num = hw.w5500_miso_gpio,
        .mosi_io_num = hw.w5500_mosi_gpio,
        .sclk_io_num = hw.w5500_sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1024,
    };

    // Keep DMA enabled so the W5500 driver can read/write larger SPI bursts.
    // Lower the queue depth to reduce outstanding DMA-backed transactions.
    err = spi_bus_initialize(W5500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOGE(TAG, "eth_init: spi_bus_initialize failed: %s", esp_err_to_name(err));
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    LOGI(TAG, "eth_init: after spi_bus_initialize DMA free=%u internal free=%u",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // SPI device config
    static spi_device_interface_config_t w5500_spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = -1,
        .queue_size = 2, // conservative: reduce outstanding DMA-backed transactions
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
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    // Install driver with default config (MAC and PHY are already configured)
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&eth_config, &eth_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "eth_init: esp_eth_driver_install failed: %s", esp_err_to_name(err));
        eth_handle = NULL;
        xSemaphoreGive(eth_lifecycle_mutex);
        return;
    }

    uint8_t mac_addr[6] = {0};
    err = esp_read_mac(mac_addr, ESP_MAC_ETH);
    if (err != ESP_OK)
    {
        LOGW(TAG, "eth_init: esp_read_mac(ESP_MAC_ETH) failed: %s; falling back to default MAC", esp_err_to_name(err));
        err = esp_efuse_mac_get_default(mac_addr);
    }

    if (err == ESP_OK)
    {
        mac_addr[0] &= 0xFE; // clear multicast bit
        mac_addr[0] |= 0x02; // set locally administered bit
        err = esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);
        if (err != ESP_OK)
        {
            LOGW(TAG, "eth_init: failed to set W5500 MAC address: %s", esp_err_to_name(err));
        }
        else
        {
            uint8_t programmed_mac[6] = {0};
            if (esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, programmed_mac) == ESP_OK)
            {
                LOGI(TAG, "eth_init: W5500 MAC programmed as %02X:%02X:%02X:%02X:%02X:%02X",
                     programmed_mac[0], programmed_mac[1], programmed_mac[2], programmed_mac[3], programmed_mac[4], programmed_mac[5]);
            }
            else
            {
                LOGI(TAG, "eth_init: set W5500 MAC to %02X:%02X:%02X:%02X:%02X:%02X",
                     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            }
        }
    }
    else
    {
        LOGW(TAG, "eth_init: could not obtain a valid MAC address for the W5500");
    }

    LOGI(TAG, "eth_init: after esp_eth_driver_install DMA free=%u internal free=%u",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Attach netif glue
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

    // Defensive guard: do not attach a netif that is already linked to lwIP.
    if (eth_netif == NULL)
    {
        LOGE(TAG, "eth_init: ETH_DEF netif vanished before attach");
        esp_eth_del_netif_glue(eth_glue);
        eth_glue = NULL;
        esp_eth_driver_uninstall(eth_handle);
        eth_handle = NULL;
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

    // Start DHCP client if configured for DHCP
    if (ethernet_config_is_dhcp_enabled())
    {
        esp_err_t d = esp_netif_dhcpc_start(eth_netif);
        if (d != ESP_OK && d != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
            LOGW(TAG, "eth_init: esp_netif_dhcpc_start failed: %s", esp_err_to_name(d));
        else
            LOGI(TAG, "eth_init: DHCP client started on ETH_DEF");
    }

    LOGI(TAG, "eth_init: exit DMA free=%u internal free=%u",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    xSemaphoreGive(eth_lifecycle_mutex);
}

/**
 * @brief Query whether the Ethernet PHY link is currently up.
 *
 * This function first attempts to query PHY link status directly via IOCTL.
 * If that is not available, it falls back to checking whether the Ethernet netif
 * has a valid IPv4 address.
 *
 * @return true if the Ethernet link is up, false otherwise.
 */
bool ethernet_link_is_up(void)
{
    if (eth_is_shutting_down())
        return false;

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

    if (eth_handle == NULL || eth_netif == NULL)
        return false;

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(eth_netif, &ip) != ESP_OK)
        return false;

    return ip.ip.addr != 0;
}

/**
 * @brief Attempt a guarded Ethernet restart when the link is down.
 *
 * This helper performs a safe restart sequence with backoff and deferred
 * teardown if the lifecycle mutex is busy.
 */
void attempt_guarded_eth_restart(void)
{
    // If link/netif appears up, skip restart (DHCP may still be negotiating)
    if (ethernet_link_is_up())
    {
        LOGI(TAG, "attempt_guarded_eth_restart: link/netif up; skipping restart");
        s_eth_restart_attempts = 0;
        return;
    }

    // Backoff limit to avoid restart storms
    if (s_eth_restart_attempts >= ETH_RESTART_MAX_ATTEMPTS)
    {
        LOGW(TAG, "attempt_guarded_eth_restart: max attempts reached; delaying longer");
        vTaskDelay(pdMS_TO_TICKS(60000));
        s_eth_restart_attempts = 0;
        return;
    }

    s_eth_restart_attempts++;
    LOGI(TAG, "attempt_guarded_eth_restart: attempt %d/%d", s_eth_restart_attempts, ETH_RESTART_MAX_ATTEMPTS);

    // Try a software restart first via the safe wrapper (which will fall back to hardware reset)
    esp_err_t rc = eth_hardware_reset_safe();
    if (rc == ESP_OK)
    {
        LOGI(TAG, "attempt_guarded_eth_restart: eth_hardware_reset_safe succeeded");

        // Request the network manager to refresh sm_actuals
        network_manager_request_actuals_update();

        // Request the syslog task to update its state (non-blocking)
        update_syslog_state();

        // Allow link/IP to settle a bit
        vTaskDelay(ETH_RESTART_BACKOFF_TICKS);
        return;
    }

    LOGW(TAG, "attempt_guarded_eth_restart: eth_hardware_reset_safe failed (%s); attempting full teardown/reinit as fallback", esp_err_to_name(rc));

    // If safe reset failed, attempt a full teardown (deferred if lifecycle busy)
    eth_teardown_safe();
    if (s_deferred_eth_teardown)
    {
        LOGW(TAG, "attempt_guarded_eth_restart: teardown deferred; will retry later");
        return;
    }

    // Small settle delay before reinit to allow any pending events to clear and avoid tight restart loops
    vTaskDelay(pdMS_TO_TICKS(200));

    // Reinitialize and start
    eth_init();
    vTaskDelay(pdMS_TO_TICKS(200));
    eth_start();

    // Allow link/IP to settle a bit before any further attempts
    vTaskDelay(ETH_RESTART_BACKOFF_TICKS);
}

/**
 * @brief Retry a previously deferred Ethernet teardown.
 *
 * Called periodically from the network manager task when teardown could not be
 * completed earlier because the lifecycle mutex was busy.
 */
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

    // Clear deferred flag and perform locked teardown
    s_deferred_eth_teardown = false;
    eth_teardown_locked();

    LOGI(TAG, "handle_deferred_eth_teardown_if_needed: deferred teardown complete");

    xSemaphoreGive(eth_lifecycle_mutex);
}

/**
 * @brief Start Ethernet driver operation, initializing first if needed.
 *
 * This function is idempotent and uses the lifecycle mutex to avoid races
 * during startup.
 */
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

    LOGI(TAG, "eth_start: before esp_eth_start DMA free=%u internal free=%u",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    esp_err_t err = esp_eth_start(eth_handle);

    if (err != ESP_OK)
    {
        LOGE(TAG, "eth_start: esp_eth_start failed: %s", esp_err_to_name(err));
        LOGW(TAG, "eth_start: retrying once after a brief stop/start cycle");

        esp_err_t stop_err = esp_eth_stop(eth_handle);
        if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE)
        {
            LOGW(TAG, "eth_start: esp_eth_stop during retry -> %s", esp_err_to_name(stop_err));
        }

        vTaskDelay(pdMS_TO_TICKS(500));
        err = esp_eth_start(eth_handle);
    }

    if (err != ESP_OK)
    {
        LOGE(TAG, "eth_start: retry start failed: %s", esp_err_to_name(err));
    }
    else
    {
        eth_started = true;
        LOGI(TAG, "eth_start: Ethernet driver started");
        LOGI(TAG, "eth_start: after esp_eth_start DMA free=%u internal free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }

    xSemaphoreGive(eth_lifecycle_mutex);
}

/**
 * @brief Perform a safe software restart of the Ethernet driver.
 *
 * This stops and restarts the currently installed Ethernet driver while the
 * lifecycle mutex is held. It is intended for runtime restart without a
 * full hardware teardown.
 *
 * @return ESP_OK if restart succeeded, otherwise the esp_err_t from
 *         the stop or start operation.
 */
esp_err_t eth_software_restart(void)
{
    if (!hw.has_ethernet)
    {
        LOGW(TAG, "eth_software_restart: Ethernet not present");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (eth_lifecycle_mutex == NULL)
        eth_lifecycle_init_once();

    if (xSemaphoreTake(eth_lifecycle_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        LOGE(TAG, "eth_software_restart: failed to take lifecycle mutex");
        return ESP_ERR_TIMEOUT;
    }

    if (eth_handle == NULL)
    {
        LOGW(TAG, "eth_software_restart: eth_handle NULL");
        xSemaphoreGive(eth_lifecycle_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    eth_set_shutting_down(true);

    esp_err_t err = esp_eth_stop(eth_handle);
    if (err != ESP_OK)
    {
        LOGW(TAG, "eth_software_restart: esp_eth_stop -> %s", esp_err_to_name(err));
    }
    eth_started = false;

    vTaskDelay(pdMS_TO_TICKS(200));

    err = esp_eth_start(eth_handle);
    if (err != ESP_OK)
    {
        LOGW(TAG, "eth_software_restart: esp_eth_start -> %s", esp_err_to_name(err));
    }
    else
    {
        eth_started = true;
        LOGI(TAG, "eth_software_restart: esp_eth_start succeeded");
    }

    eth_set_shutting_down(false);
    xSemaphoreGive(eth_lifecycle_mutex);
    return err;
}

/**
 * @brief Stop Ethernet without destroying resources needed by the RX task.
 *
 * Runtime configuration changes can arrive while the W5500 RX task is still
 * executing on the other CPU. Keep the netif, glue, and driver allocated so
 * an in-flight packet callback cannot access freed memory.
 */
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

    eth_set_shutting_down(true);
    if (eth_handle && eth_started)
    {
        esp_err_t r = esp_eth_stop(eth_handle);
        if (r != ESP_OK && r != ESP_ERR_INVALID_STATE)
            LOGW(TAG, "eth_stop: esp_eth_stop -> %s", esp_err_to_name(r));
        eth_started = false;
    }
    eth_set_shutting_down(false);

    LOGI(TAG, "eth_stop: Ethernet driver stopped; resources retained for safe restart");

    xSemaphoreGive(eth_lifecycle_mutex);
}

/**
 * @brief Check whether Ethernet is enabled in the network configuration.
 *
 * @return true if Ethernet is enabled in the stored configuration, false otherwise.
 */
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
        // A transient lock failure must not log here: logging may query the
        // active interface again and recursively re-enter this function.
    }
    return enabled;
}

/**
 * @brief Check whether DHCP is enabled for Ethernet in the network configuration.
 *
 * @return true if Ethernet DHCP is enabled in the stored configuration, false otherwise.
 */
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
        // Keep the DHCP default on transient lock failure without logging.
    }
    return dhcp;
}

/**
 * @brief Apply the current Ethernet configuration from the network config.
 *
 * This function configures the Ethernet hostname, starts or stops the driver,
 * and applies DHCP or static IPv4 settings as requested by the configuration.
 *
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
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
        LOGW(TAG, "Invalid static IP configuration; keeping the current Ethernet IP");
        return ESP_ERR_INVALID_ARG;
    }

    // Switch cleanly from DHCP to static addressing only after the complete
    // static configuration has been validated.
    esp_err_t dhcp_err = esp_netif_dhcpc_stop(eth_netif);
    if (dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
    {
        LOGE(TAG, "esp_netif_dhcpc_stop failed: %s", esp_err_to_name(dhcp_err));
        return dhcp_err;
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

/**
 * @brief Mark Ethernet as currently shutting down.
 *
 * @param v true to indicate shutdown is in progress, false otherwise.
 */
void eth_set_shutting_down(bool v)
{
    s_eth_shutting_down = v;
}

/**
 * @brief Check whether Ethernet is currently in shutdown state.
 *
 * @return true when a shutdown is in progress, false otherwise.
 */
bool eth_is_shutting_down(void)
{
    return s_eth_shutting_down;
}

/**
 * @brief Retrieve the current Ethernet driver handle.
 *
 * @return The current esp_eth_handle_t, or NULL if Ethernet is not initialized.
 */
esp_eth_handle_t eth_get_handle(void)
{
    return eth_handle;
}
