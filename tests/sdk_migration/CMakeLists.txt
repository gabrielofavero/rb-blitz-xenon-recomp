cmake_minimum_required(VERSION 3.25)
project(rb_blitz_sdk_migration LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(rexglue 0.11.0 EXACT REQUIRED CONFIG)
include(CTest)

set(RBBLITZ_MIGRATION_XEX "" CACHE FILEPATH "Supported default.xex for isolated codegen")
if(NOT EXISTS "${RBBLITZ_MIGRATION_XEX}")
    message(FATAL_ERROR "Set RBBLITZ_MIGRATION_XEX to the supported default.xex")
endif()

get_filename_component(RBBLITZ_MIGRATION_XEX "${RBBLITZ_MIGRATION_XEX}" ABSOLUTE)
get_filename_component(RBBLITZ_MIGRATION_GAME_ROOT "${RBBLITZ_MIGRATION_XEX}" DIRECTORY)
get_filename_component(RBBLITZ_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(TO_CMAKE_PATH "${RBBLITZ_MIGRATION_XEX}" RBBLITZ_MIGRATION_XEX)
file(TO_CMAKE_PATH "${RBBLITZ_MIGRATION_GAME_ROOT}" RBBLITZ_MIGRATION_GAME_ROOT)

set(RBBLITZ_WINDOWS_TEST_RUNNER "" CACHE FILEPATH "Built Windows payload test launcher")
if(WIN32 AND NOT EXISTS "${RBBLITZ_WINDOWS_TEST_RUNNER}")
    message(FATAL_ERROR "Set RBBLITZ_WINDOWS_TEST_RUNNER to rb_blitz_windows_test_runner.exe")
endif()

# Keep the manifest and all generated guest output inside this independent build.
configure_file(CMakeLists.txt.in title/CMakeLists.txt @ONLY)
configure_file(rb_blitz_manifest.toml.in title/rb_blitz_manifest.toml @ONLY)
add_subdirectory("${CMAKE_CURRENT_BINARY_DIR}/title" title-build)

add_executable(trace_probe ../sdk_migration_trace_probe.cpp)
target_link_libraries(trace_probe PRIVATE rex::sdk)
rexglue_configure_target(trace_probe)
if(WIN32)
    add_test(NAME trace_probe COMMAND "${RBBLITZ_WINDOWS_TEST_RUNNER}" $<TARGET_FILE:trace_probe>)
else()
    add_test(NAME trace_probe COMMAND $<TARGET_FILE:trace_probe>)
endif()

add_executable(preparation_probe ../sdk_migration_preparation_probe.cpp)
target_link_libraries(preparation_probe PRIVATE rex::sdk)
rexglue_configure_target(preparation_probe)
if(WIN32)
    add_test(NAME preparation_probe COMMAND "${RBBLITZ_WINDOWS_TEST_RUNNER}" $<TARGET_FILE:preparation_probe>)
else()
    add_test(NAME preparation_probe COMMAND $<TARGET_FILE:preparation_probe>)
endif()

# Each pair stages runtime DLLs into one directory. Order their staging steps.
add_dependencies(preparation_probe trace_probe)
add_dependencies(registration_empty_probe registration_probe)
