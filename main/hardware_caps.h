/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include <stdbool.h>

typedef enum
{
    HW_VERSION_2 = 2,
    HW_VERSION_3 = 3
} hardware_version_t;

typedef struct
{
    bool has_ethernet;
    bool has_wifi_led;
    bool has_eth_led;

    // LED pins
    int pin_wifi_led;
    int pin_eth_led;

    // I2C hardware configuration
    int i2c_master_bus0_sda_io;
    int i2c_master_bus0_scl_io;
    int i2c_master_bus1_sda_io;
    int i2c_master_bus1_scl_io;

    // Ethernet hardware configuration
    int w5500_miso_gpio;
    int w5500_mosi_gpio;
    int w5500_sclk_gpio;
    int w5500_cs_gpio;
    int w5500_int_gpio;
    int w5500_rst_gpio;

    // 74595 pins circuit 1
    int clockpin1;
    int latchpin1;
    int datapin1;

    // 74595 pins circuit 2
    int clockpin2;
    int latchpin2;
    int datapin2;

    // RGB LEDs pin
    int rgb_led_pin;

    // Switches pins
    int switch1_pin;
    int switch2_pin;

} hw_caps_t;

extern hardware_version_t hw_version;
extern hw_caps_t hw;

void hw_init(void); // call once at startup
