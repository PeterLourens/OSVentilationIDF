/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef CC1101_H
#define CC1101_H

#include "driver/spi_master.h"
#include "esp_err.h"

#define CC1101_SPI_HOST SPI3_HOST
#define CC1101_SPI_CLOCK_HZ (1 * 1000 * 1000)
#define CC1101_STROBE_RESET 0x30
#define CC1101_STATUS_PARTNUM 0x30
#define CC1101_STATUS_VERSION 0x31
#define CC1101_READ_BURST 0xC0
#define CC1101_CHIP_READY_TIMEOUT_US 10000

/**
 * @brief Initialize and probe the CC1101 on supported hardware.
 *
 * The probe resets the radio and reads its PARTNUM and VERSION registers.
 * Boards without a CC1101 return ESP_OK without initializing SPI.
 *
 * @return ESP_OK when the radio is detected or not present on this board,
 *         ESP_ERR_NOT_FOUND when the configured radio is not responding,
 *         or another ESP error if interface initialization fails.
 */
esp_err_t cc1101_init(void);

#endif
