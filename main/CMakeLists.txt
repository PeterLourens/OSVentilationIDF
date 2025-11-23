idf_component_register(SRCS "http_functions.c" "rgb_leds.c" "task_rgb_leds.c" "config_files.c" "wifi_functions.c" "task_wifi.c" "task_i2c_ds3231.c" "i2c_functions.c" "i2c_ds3231.c" "i2c_sensors.c" 
                    "task_i2c_sensors.c" "main.c" "globals.c" "i2c_sensors.c" "task_i2c_sensors.c" "general.c" "i2c_functions.c" "i2c_ds3231.c" "task_wifi.c" "task_i2c_lcd.c" "i2c_lcd.c" 
                    "config_files.c" "rgb_leds.c" "task_rgb_leds.c" "valvecontrol.c" "http_functions.c" "syslog.c" "console.c" "sys_stats.c" "websocket_func.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "../web/index.html" "../web/webserial.html" "../web/pure_min.css" "../web/styles.css" "../web/styles.css" "../web/ui.js" "../web/webserial.js" 
                                    "../web/stats.html" "../web/stats.js" "../web/sensor_config.html" "../web/settings.html" "../web/statemachine.html" "../web/valvecontrol.html"
                                    "../web/index_ws.js"
                    EMBED_FILES "../web/favicon.ico"
)
littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT)
