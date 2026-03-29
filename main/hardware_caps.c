// hardware_caps.c
#include "hardware_caps.h"

// default: build for v3 in this branch
hardware_version_t hw_version = HW_VERSION_3;

static const hw_caps_t hw_v2 = {
    .has_ethernet = false,
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

    .clockpin1 = 11,
    .latchpin1 = 12,
    .datapin1 = 13,

    .clockpin2 = 15,
    .latchpin2 = 16,
    .datapin2 = 14,
};

static const hw_caps_t hw_v3 = {
    .has_ethernet = true,
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

    .clockpin1 = 1,
    .latchpin1 = 2,
    .datapin1 = 3,

    .clockpin2 = 15,
    .latchpin2 = 16,
    .datapin2 = 14,
};
hw_caps_t hw;

void hw_init(void)
{
    switch (hw_version)
    {
    case HW_VERSION_2:
        hw = hw_v2;
        break;
    case HW_VERSION_3:
    default:
        hw = hw_v3;
        break;
    }
}
