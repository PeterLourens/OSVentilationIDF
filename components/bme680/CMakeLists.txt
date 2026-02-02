cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_timer esp_system
)