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

# Asset virtual filesystem (BSA + loose files) and scene assembly.
add_library(onv_assets assets/data_files.cpp)
target_include_directories(onv_assets PUBLIC assets)
target_link_libraries(onv_assets PUBLIC onv_formats)

add_library(onv_scene scene/scene.cpp)
target_include_directories(onv_scene PUBLIC scene)
target_link_libraries(onv_scene PUBLIC onv_records onv_assets)

# CPU-side render support (texture decode/cache).
add_library(onv_render render/texture_cache.cpp)
target_include_directories(onv_render PUBLIC render)
target_link_libraries(onv_render PUBLIC onv_assets onv_formats)

add_executable(worldexport tools/worldexport.cpp)
target_link_libraries(worldexport PRIVATE onv_records)

add_executable(scenedump tools/scenedump.cpp)
target_link_libraries(scenedump PRIVATE onv_scene)

add_executable(laa_patch tools/laa_patch.cpp)

find_package(SDL2 QUIET)
find_package(OpenGL QUIET)
if(SDL2_FOUND AND OpenGL_FOUND)
  add_executable(walker walker/main.cpp)
  target_include_directories(walker PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(walker PRIVATE
    onv_terrain onv_records onv_platform onv_scene onv_assets onv_render
    ${SDL2_LIBRARIES} OpenGL::GL)
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

add_executable(data_files_tests tests/data_files_tests.cpp)
target_link_libraries(data_files_tests PRIVATE onv_assets)
add_test(NAME data_files_tests COMMAND data_files_tests)

add_executable(scene_tests tests/scene_tests.cpp)
target_link_libraries(scene_tests PRIVATE onv_scene)
add_test(NAME scene_tests COMMAND scene_tests)

add_executable(texture_cache_tests tests/texture_cache_tests.cpp)
target_link_libraries(texture_cache_tests PRIVATE onv_render)
add_test(NAME texture_cache_tests COMMAND texture_cache_tests)

add_executable(integration_tests tests/integration_tests.cpp)
target_link_libraries(integration_tests PRIVATE onv_scene onv_render onv_platform)
add_test(NAME integration_tests COMMAND integration_tests)

add_executable(robustness_tests tests/robustness_tests.cpp)
target_link_libraries(robustness_tests PRIVATE onv_scene onv_render)
add_test(NAME robustness_tests COMMAND robustness_tests)
