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

# Asset/plugin formats: archives, plugins, meshes, textures
add_library(onv_formats formats/bsa.cpp formats/esm.cpp formats/nif.cpp formats/dds.cpp)
target_include_directories(onv_formats PUBLIC formats)
target_link_libraries(onv_formats PUBLIC ZLIB::ZLIB)

# Platform services: locating the user's game install, etc.
add_library(onv_platform platform/game_locator.cpp)
target_include_directories(onv_platform PUBLIC platform)

add_executable(export_heightmap tools/export_heightmap.cpp)
target_link_libraries(export_heightmap PRIVATE onv_terrain)

add_executable(bsatool tools/bsatool.cpp)
target_link_libraries(bsatool PRIVATE onv_formats)

add_executable(esmdump tools/esmdump.cpp)
target_link_libraries(esmdump PRIVATE onv_formats)

add_library(onv_records records/records.cpp)
target_include_directories(onv_records PUBLIC records)
target_link_libraries(onv_records PUBLIC onv_formats)

add_executable(worldexport tools/worldexport.cpp)
target_link_libraries(worldexport PRIVATE onv_records)

add_executable(laa_patch tools/laa_patch.cpp)

find_package(SDL2 QUIET)
find_package(OpenGL QUIET)
if(SDL2_FOUND AND OpenGL_FOUND)
  add_executable(walker walker/main.cpp)
  target_include_directories(walker PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(walker PRIVATE
    onv_terrain onv_records onv_platform ${SDL2_LIBRARIES} OpenGL::GL)
else()
  message(STATUS "SDL2/OpenGL not found — skipping native walker")
endif()

enable_testing()
add_executable(format_tests tests/format_tests.cpp)
target_link_libraries(format_tests PRIVATE onv_formats)
add_test(NAME format_tests COMMAND format_tests)

add_executable(records_tests tests/records_tests.cpp)
target_link_libraries(records_tests PRIVATE onv_records)
add_test(NAME records_tests COMMAND records_tests)

add_executable(nif_tests tests/nif_tests.cpp)
target_link_libraries(nif_tests PRIVATE onv_formats)
add_test(NAME nif_tests COMMAND nif_tests)

add_executable(dds_tests tests/dds_tests.cpp)
target_link_libraries(dds_tests PRIVATE onv_formats)
add_test(NAME dds_tests COMMAND dds_tests)
