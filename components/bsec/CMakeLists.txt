cmake_minimum_required(VERSION 3.16)

cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRC_DIRS "src" "../bme680/src"
    INCLUDE_DIRS "include" "../bme680/include"
    REQUIRES driver freertos esp_timer esp_system
)

#idf_component_register(
#    SRCS "src/bme68x.c" "src/bsec_iaq.c" "src/bsec_integration.c" "src/bsec_selectivity.c"   # remove or adjust if you don't have a wrapper source
#    INCLUDE_DIRS "include"
#    REQUIRES driver
#)

# Link the imported library into the component target
target_link_libraries(${COMPONENT_LIB}
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libalgobsec.a"
)
