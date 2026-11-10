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

include(CTest)
if(BUILD_TESTING)
    add_executable(rb_blitz_portable_import_tests ../tests/portable_import_tests.cpp)
    target_link_libraries(rb_blitz_portable_import_tests PRIVATE rb_blitz_game_import_core)
    target_compile_definitions(rb_blitz_portable_import_tests PRIVATE
        RBBLITZ_ROOT_DIR="${RBBLITZ_ROOT}")
    add_test(NAME portable_import COMMAND rb_blitz_portable_import_tests)
endif()
