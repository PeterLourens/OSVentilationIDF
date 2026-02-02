cmake_minimum_required(VERSION 3.16)

cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRC_DIRS "src" "../bme680/src"
    INCLUDE_DIRS "include" "../bme680/include"
    REQUIRES driver freertos esp_timer esp_system
)

# Link the imported library into the component target
target_link_libraries(${COMPONENT_LIB}
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libalgobsec.a"
)
