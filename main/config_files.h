#ifndef CONFIG_FILES_H
#define CONFIG_FILES_H

#include "globals.h"
#include "general.h"

//Function definitions
esp_err_t load_sensor_config(const char *);
esp_err_t load_network_config(void);
esp_err_t load_time_config(void);

#endif