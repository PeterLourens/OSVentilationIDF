#ifndef GENERAL_H
#define GENERAL_H

#include "globals.h"

#include "esp_littlefs.h"   // Mounting littlefs
#include "esp_partition.h"  // Accessing flash partitions
#include "wear_levelling.h" // Wear leveling support (for flash-based FATFS)
#include "nvs_flash.h"      // NVS initialization
#include "nvs.h"            // NVS read/write APIs
#include "cJSON.h"          // JSON parsing
#include <dirent.h>         // Directory operations

// Function declarations
esp_err_t init_nvs(void);
esp_err_t init_littlefs(void);
esp_err_t read_file(const char *, char *, size_t);
esp_err_t load_sensor_config(const char *);

#endif