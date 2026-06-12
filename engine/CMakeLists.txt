cmake_minimum_required(VERSION 3.16)
project(OpenNewVegas CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(onv_terrain terrain/terrain.cpp)
target_include_directories(onv_terrain PUBLIC terrain)

add_executable(export_heightmap tools/export_heightmap.cpp)
target_link_libraries(export_heightmap PRIVATE onv_terrain)
