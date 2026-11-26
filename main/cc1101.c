/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "cc1101.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "hardware_caps.h"

static const char *TAG = "cc1101";
static spi_device_handle_t s_cc1101_device;

static esp_err_t cc1101_spi_transmit(spi_transaction_t *transaction)
{
    gpio_num_t cs_pin = (gpio_num_t)hw.cc1101_cs_gpio;
    gpio_num_t miso_pin = (gpio_num_t)hw.cc1101_miso_gpio;

    esp_err_t err = gpio_set_level(cs_pin, 0);
    if (err != ESP_OK)
    {
        return err;
    }

    uint32_t timeout_us = CC1101_CHIP_READY_TIMEOUT_US;
    while (timeout_us > 0 && gpio_get_level(miso_pin) != 0)
    {
        esp_rom_delay_us(1);
        timeout_us--;
    }
    if (gpio_get_level(miso_pin) != 0)
    {
        esp_err_t release_err = gpio_set_level(cs_pin, 1);
        if (release_err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to release CC1101 chip select: %s", esp_err_to_name(release_err));
            return release_err;
        }
        return ESP_ERR_TIMEOUT;
    }

    err = spi_device_transmit(s_cc1101_device, transaction);
    esp_err_t cs_err = gpio_set_level(cs_pin, 1);
    if (cs_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to release CC1101 chip select: %s", esp_err_to_name(cs_err));
    }
    return err == ESP_OK ? cs_err : err;
}

static esp_err_t cc1101_send_strobe(uint8_t strobe)
{
    spi_transaction_t transaction = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 8,
    };
    transaction.tx_data[0] = strobe;
    return cc1101_spi_transmit(&transaction);
}

static esp_err_t cc1101_read_status_register(uint8_t address, uint8_t *value)
{
    spi_transaction_t transaction = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 16,
    };
    transaction.tx_data[0] = address | CC1101_READ_BURST;
    transaction.tx_data[1] = 0;

    esp_err_t err = cc1101_spi_transmit(&transaction);
    if (err == ESP_OK)
    {
        *value = transaction.rx_data[1];
    }
    return err;
}

static void cc1101_release_interface(void)
{
    if (s_cc1101_device)
    {
        esp_err_t err = spi_bus_remove_device(s_cc1101_device);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to remove CC1101 SPI device: %s", esp_err_to_name(err));
        }
        s_cc1101_device = NULL;
    }
    esp_err_t err = spi_bus_free(CC1101_SPI_HOST);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to free CC1101 SPI bus: %s", esp_err_to_name(err));
    }
}

esp_err_t cc1101_init(void)
{
    if (!hw.has_cc1101)
    {
        ESP_LOGI(TAG, "No CC1101 configured for hardware revision %d", hw_version);
        return ESP_OK;
    }

    if (s_cc1101_device)
    {
        return ESP_OK;
    }

    if (hw.cc1101_miso_gpio < 0 || hw.cc1101_mosi_gpio < 0 ||
        hw.cc1101_sclk_gpio < 0 || hw.cc1101_cs_gpio < 0 ||
        hw.cc1101_int_gpio < 0)
    {
        ESP_LOGE(TAG, "CC1101 is enabled but one or more GPIOs are not configured");
        return ESP_ERR_INVALID_ARG;
    }

    spi_bus_config_t bus_config = {
        .mosi_io_num = hw.cc1101_mosi_gpio,
        .miso_io_num = hw.cc1101_miso_gpio,
        .sclk_io_num = hw.cc1101_sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 2,
    };

    esp_err_t err = spi_bus_initialize(CC1101_SPI_HOST, &bus_config, SPI_DMA_DISABLED);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = CC1101_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };

    gpio_config_t cs_config = {
        .pin_bit_mask = 1ULL << hw.cc1101_cs_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&cs_config);
    if (err == ESP_OK)
    {
        err = gpio_set_level((gpio_num_t)hw.cc1101_cs_gpio, 1);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure CC1101 chip select: %s", esp_err_to_name(err));
        cc1101_release_interface();
        return err;
    }

    err = spi_bus_add_device(CC1101_SPI_HOST, &device_config, &s_cc1101_device);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add CC1101 SPI device: %s", esp_err_to_name(err));
        cc1101_release_interface();
        return err;
    }

    gpio_config_t gdo_config = {
        .pin_bit_mask = 1ULL << hw.cc1101_int_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&gdo_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure CC1101 GDO input: %s", esp_err_to_name(err));
        cc1101_release_interface();
        return err;
    }

    err = cc1101_send_strobe(CC1101_STROBE_RESET);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to reset CC1101: %s", esp_err_to_name(err));
        cc1101_release_interface();
        return err;
    }
    esp_rom_delay_us(1000);

    uint8_t part_number = 0;
    uint8_t version = 0;
    err = cc1101_read_status_register(CC1101_STATUS_PARTNUM, &part_number);
    if (err == ESP_OK)
    {
        err = cc1101_read_status_register(CC1101_STATUS_VERSION, &version);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read CC1101 identification registers: %s", esp_err_to_name(err));
        cc1101_release_interface();
        return err;
    }

    if (part_number != 0x00 || version == 0x00 || version == 0xFF)
    {
        ESP_LOGW(TAG, "CC1101 not detected (PARTNUM=0x%02X, VERSION=0x%02X)",
                 part_number, version);
        cc1101_release_interface();
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "CC1101 detected (PARTNUM=0x%02X, VERSION=0x%02X)",
             part_number, version);
    return ESP_OK;
}
