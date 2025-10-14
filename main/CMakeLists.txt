idf_component_register(SRCS "task_i2c.c" "i2c_functions.c" "main.c" "globals.c" "i2c_functions.c" "task_i2c.c" "general.c"
                    INCLUDE_DIRS ".")
littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT)
