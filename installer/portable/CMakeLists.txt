# SDK-free game-source importer, built on each host without Inno Setup or the
# ReXGlue runtime. The caller supplies the supported fingerprint record and a
# native picker/progress implementation.
cmake_minimum_required(VERSION 3.25)
project(rb_blitz_portable_import LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RBBLITZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_library(rb_blitz_game_import_core STATIC
    ../src/game_import_core.cpp
    ../src/stfs.cpp
    ../src/inflate.cpp
    ../src/util.cpp
    "${RBBLITZ_ROOT}/src/util/game_fingerprint.cpp"
    "${RBBLITZ_ROOT}/src/util/sha256.cpp")
target_include_directories(rb_blitz_game_import_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${RBBLITZ_ROOT}/src")
target_compile_features(rb_blitz_game_import_core PUBLIC cxx_std_20)
if(WIN32)
    target_compile_definitions(rb_blitz_game_import_core PRIVATE
        _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

add_executable(rb_blitz_import import_main.cpp)
if(NOT APPLE)
    target_link_libraries(rb_blitz_import PRIVATE rb_blitz_game_import_core)
endif()

if(APPLE)
    include(ultimate.cmake)
    target_link_libraries(rb_blitz_import PRIVATE rb_blitz_ultimate_import_core)
    target_compile_definitions(rb_blitz_import PRIVATE RBBLITZ_ULTIMATE_IMPORT=1)
    # Also prove the actual Finder entry point compiles without building the SDK
    # or requiring a private game dump. This is the same launcher shipped in the app.
    enable_language(OBJCXX)
    include(../macos/setup.cmake)
    add_executable(rb_blitz_macos_launcher ../macos/importer_main.mm)
    set_target_properties(rb_blitz_macos_launcher PROPERTIES
        OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
    target_link_libraries(rb_blitz_macos_launcher PRIVATE
        rb_blitz_ultimate_import_core rb_blitz_macos_setup "-framework Cocoa")
endif()

include(CTest)
if(BUILD_TESTING)
    if(APPLE)
        add_executable(rb_blitz_macos_setup_tests ../tests/macos_setup_tests.cpp)
        target_include_directories(rb_blitz_macos_setup_tests PRIVATE "${RBBLITZ_ROOT}/tests")
        target_link_libraries(rb_blitz_macos_setup_tests PRIVATE rb_blitz_macos_setup)
        add_test(NAME macos_setup COMMAND rb_blitz_macos_setup_tests)
        add_executable(rb_blitz_ultimate_cancel_tests ../tests/ultimate_cancel_tests.cpp)
        target_link_libraries(rb_blitz_ultimate_cancel_tests PRIVATE rb_blitz_ultimate_import_core)
        add_test(NAME ultimate_cancel COMMAND rb_blitz_ultimate_cancel_tests)
    endif()
    add_executable(rb_blitz_portable_import_tests ../tests/portable_import_tests.cpp)
    target_link_libraries(rb_blitz_portable_import_tests PRIVATE rb_blitz_game_import_core)
    target_compile_definitions(rb_blitz_portable_import_tests PRIVATE
        RBBLITZ_ROOT_DIR="${RBBLITZ_ROOT}")
    add_test(NAME portable_import COMMAND rb_blitz_portable_import_tests)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME import_cli COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tests/import_cli_tests.py" $<TARGET_FILE:rb_blitz_import>)
        if(APPLE)
            add_test(NAME ultimate_cli COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../tests/ultimate_cli_tests.py" $<TARGET_FILE:rb_blitz_import>)
        endif()
    endif()
endif()
