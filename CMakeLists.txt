# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PROJECT_PARTITION_CSV ${CMAKE_SOURCE_DIR}/partitions.csv)

# Import the prebuilt static library for Bosch BSEC
#add_library(libalgobsec STATIC IMPORTED)

# Path to the .a file relative to the project root (adjust if different)
#set_target_properties(libalgobsec PROPERTIES
#    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/components/bsec/lib/libalgobsec.a"
#)

project(OSVentilationIDF)

