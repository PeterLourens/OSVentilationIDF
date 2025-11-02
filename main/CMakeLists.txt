idf_component_register(SRCS "wifi_functions.c" "task_wifi.c" "task_i2c_ds3231.c" "i2c_functions.c" "i2c_ds3231.c" "i2c_sensors.c" "task_i2c_sensors.c" "main.c" "globals.c" "i2c_sensors.c" 
                    "task_i2c_sensors.c" "general.c" "i2c_functions.c" "i2c_ds3231.c" "task_wifi.c" "task_i2c_lcd.c" "i2c_lcd.c"
                    INCLUDE_DIRS ".")
littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT)
