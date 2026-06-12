cmake_minimum_required(VERSION 3.16)
project(OpenNewVegas CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(ZLIB REQUIRED)

add_library(onv_terrain terrain/terrain.cpp)
target_include_directories(onv_terrain PUBLIC terrain)

add_library(onv_formats formats/bsa.cpp formats/esm.cpp)
target_include_directories(onv_formats PUBLIC formats)
target_link_libraries(onv_formats PUBLIC ZLIB::ZLIB)

add_executable(export_heightmap tools/export_heightmap.cpp)
target_link_libraries(export_heightmap PRIVATE onv_terrain)

add_executable(bsatool tools/bsatool.cpp)
target_link_libraries(bsatool PRIVATE onv_formats)

add_executable(esmdump tools/esmdump.cpp)
target_link_libraries(esmdump PRIVATE onv_formats)

enable_testing()
add_executable(format_tests tests/format_tests.cpp)
target_link_libraries(format_tests PRIVATE onv_formats)
add_test(NAME format_tests COMMAND format_tests)
