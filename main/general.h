#ifndef GENERAL_H
#define GENERAL_H

#include "globals.h"

#include "esp_vfs_fat.h"    // Mounting FATFS
#include "esp_partition.h"  // Accessing flash partitions
#include "wear_levelling.h" // Wear leveling support (for flash-based FATFS)
#include "nvs_flash.h"      // NVS initialization
#include "nvs.h"            // NVS read/write APIs

#endif