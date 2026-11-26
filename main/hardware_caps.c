/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file hardware_caps.c
 * @brief Board capability profiles and hardware initialization.
 *
 * This module defines the supported hardware capability sets for board
 * revisions and selects the active configuration at startup.
 */

#include "hardware_caps.h"

// default: build for v4 in this branch
hardware_version_t hw_version = HW_VERSION_4;

/**
 * @brief Hardware capability profile for board version 2. This version is without Ether
 */
static const hw_caps_t hw_v2 = {
    .has_ethernet = false,
    .has_cc1101 = false,
    .has_wifi_led = false,
    .has_eth_led = false,

    .pin_wifi_led = -1,
    .pin_eth_led = -1,

    .i2c_master_bus0_sda_io = 39,
    .i2c_master_bus0_scl_io = 38,
    .i2c_master_bus1_sda_io = 41,
    .i2c_master_bus1_scl_io = 40,

    .w5500_miso_gpio = -1,
    .w5500_mosi_gpio = -1,
    .w5500_sclk_gpio = -1,
    .w5500_cs_gpio = -1,
    .w5500_int_gpio = -1,
    .w5500_rst_gpio = -1,

    .cc1101_miso_gpio = -1,
    .cc1101_mosi_gpio = -1,
    .cc1101_sclk_gpio = -1,
    .cc1101_cs_gpio = -1,
    .cc1101_int_gpio = -1,

    .uart0_tx_gpio = -1,
    .uart0_rx_gpio = -1,

    .clockpin1 = 11,
    .latchpin1 = 12,
    .datapin1 = 13,

    .clockpin2 = 15,
    .latchpin2 = 16,
    .datapin2 = 14,

    .rgb_led_pin = 42,

    .switch1_pin = 6,
    .switch2_pin = 7,
};

/**
 * @brief Hardware capability profile for board version 4.
 */
static const hw_caps_t hw_v4 = {
    .has_ethernet = true,
    .has_cc1101 = false,
    .has_wifi_led = true,
    .has_eth_led = true,

    .pin_wifi_led = 45,
    .pin_eth_led = 46,

    .i2c_master_bus0_sda_io = 18,
    .i2c_master_bus0_scl_io = 17,
    .i2c_master_bus1_sda_io = 7,
    .i2c_master_bus1_scl_io = 6,

    .w5500_miso_gpio = 13,
    .w5500_mosi_gpio = 11,
    .w5500_sclk_gpio = 12,
    .w5500_cs_gpio = 10,
    .w5500_int_gpio = 9,
    .w5500_rst_gpio = 8,

    .cc1101_miso_gpio = -1,
    .cc1101_mosi_gpio = -1,
    .cc1101_sclk_gpio = -1,
    .cc1101_cs_gpio = -1,
    .cc1101_int_gpio = -1,

    .uart0_tx_gpio = 43,
    .uart0_rx_gpio = 44,

    .clockpin1 = 1,
    .latchpin1 = 2,
    .datapin1 = 3,

    .clockpin2 = 15,
    .latchpin2 = 16,
    .datapin2 = 14,

    .rgb_led_pin = 47,

    .switch1_pin = 4,
    .switch2_pin = 5,
};
hw_caps_t hw;

/**
 * @brief Hardware capability profile for board version 5.
 */
static const hw_caps_t hw_v5 = {
    .has_ethernet = true,
    .has_cc1101 = true,
    .has_wifi_led = true,
    .has_eth_led = true,

    .pin_wifi_led = 45,
    .pin_eth_led = 46,

    .i2c_master_bus0_sda_io = 18,
    .i2c_master_bus0_scl_io = 17,
    .i2c_master_bus1_sda_io = 7,
    .i2c_master_bus1_scl_io = 6,

    .w5500_miso_gpio = 13,
    .w5500_mosi_gpio = 11,
    .w5500_sclk_gpio = 12,
    .w5500_cs_gpio = 10,
    .w5500_int_gpio = 9,
    .w5500_rst_gpio = 8,

    .cc1101_miso_gpio = 16,
    .cc1101_mosi_gpio = 14,
    .cc1101_sclk_gpio = 15,
    .cc1101_cs_gpio = 21,
    .cc1101_int_gpio = 5,

    .uart0_tx_gpio = 43,
    .uart0_rx_gpio = 44,

    .clockpin1 = 1,
    .latchpin1 = 2,
    .datapin1 = 3,

    .rgb_led_pin = 47,

    .switch1_pin = 4,
    .switch2_pin = 48,
};
hw_caps_t hw;

/**
 * @brief Initialize the active hardware capability table.
 *
 * This function selects the appropriate board profile based on
 * @c hw_version and copies it into the runtime @c hw structure.
 */
void hw_init(void)
{
    switch (hw_version)
    {
    case HW_VERSION_2:
        hw = hw_v2;
        break;
    case HW_VERSION_4:
        hw = hw_v4;
        break;
    case HW_VERSION_5:
    default:
        hw = hw_v5;
        break;
    }
}
