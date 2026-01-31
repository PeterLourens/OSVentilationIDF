# Use GNU C11 (recommended for ESP-IDF compatibility)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # use gnu11; remove or set OFF for strict C11

idf_component_register(SRCS "bme680_test.c" "task_valvecontrol.c" "fancontrol.c" "task_statemachine.c" "http_helper.c" "http_post_functions.c" "http_functions.c" "rgb_leds.c" "task_rgb_leds.c" "config_files.c" "wifi_functions.c" "task_wifi.c" "task_i2c_ds3231.c" "i2c_functions.c" 
                    "i2c_ds3231.c" "i2c_sensors.c" "task_i2c_sensors.c" "main.c" "globals.c" "i2c_sensors.c" "task_i2c_sensors.c" "general.c" "i2c_functions.c" "i2c_ds3231.c" "task_wifi.c" "task_i2c_lcd.c" 
                    "i2c_lcd.c" "config_files.c" "rgb_leds.c" "task_rgb_leds.c" "valvecontrol.c" "http_functions.c" "syslog.c" "sys_stats.c" "http_send_ws_data.c" "http_post_functions.c"
                    "http_helper.c" "task_statemachine" "statemachine.c" "statemachine_helper.c" "task_valvecontrol.c" "task_mqtt.c" "mqtt.c" "task_influxdb.c" "influxdb.c"
                    "bsec_adapter.c" "bme680_driver.c" "bme68x.c" "bme680_test.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "../web/index.html" "../web/webserial.html" "../web/pure_min.css" "../web/styles.css" "../web/ui.js" "../web/stats.html" 
                                    "../web/sensor_config.html" "../web/config.html" "../web/statemachine.html" "../web/valvecontrol.html" "../web/send_json_ws.js"
                                    "../web/reboot.html"
                    EMBED_FILES "../web/favicon.ico"
)
littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT)
