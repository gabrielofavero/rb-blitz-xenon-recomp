# Standalone installer for Rock Band Blitz (see installer/README.md).
#
# This is a separate CMake project on purpose: the installer is shipped as a
# Windows executable, not as part of the emulator build, and it must build on a
# machine that has no recompiled output and no game data. It therefore reuses
# only two project-owned files from the emulator tree (src/util/sha256.cpp and
# src/util/game_fingerprint.cpp), so the fingerprint rules the installer
# enforces are literally the ones the runtime enforces.

cmake_minimum_required(VERSION 3.25)

project(rb_blitz_installer LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "installer: Windows only (STFS, WinHTTP, Inno Setup)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# _wfopen is used deliberately (UTF-8 paths on a machine whose ANSI code page may
# not be UTF-8), so silence the CRT's deprecation for it rather than swapping in a
# non-portable alternative.
# <windows.h> without the parts this tool does not use, and without the min/max
# macros that would collide with <algorithm>.
add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE WIN32_LEAN_AND_MEAN
                        NOMINMAX)

set(RBBLITZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(RBBLITZ_INSTALLER_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")

# --------------------------------------------------------------------------
# Generated config: the release pins and the fingerprint files, embedded as
# string literals so the helper is self-contained (it runs from a temporary
# directory on a machine that has never seen this repository).
# --------------------------------------------------------------------------
add_executable(rb_blitz_embed_config tools/embed_config.cpp)
target_compile_features(rb_blitz_embed_config PRIVATE cxx_std_20)

set(RBBLITZ_EMBEDDED_CONFIG "${RBBLITZ_INSTALLER_GENERATED}/embedded_config.h")
set(RBBLITZ_ISPP_PINS "${RBBLITZ_INSTALLER_GENERATED}/pins.iss")

set(RBBLITZ_CONFIG_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/config/pins.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/config/ultimate_fingerprints.toml"
    "${RBBLITZ_ROOT}/config/game_fingerprints.toml")

add_custom_command(
    OUTPUT "${RBBLITZ_EMBEDDED_CONFIG}" "${RBBLITZ_ISPP_PINS}"
    COMMAND rb_blitz_embed_config
            --pins "${CMAKE_CURRENT_SOURCE_DIR}/config/pins.toml"
            --game-fingerprints "${RBBLITZ_ROOT}/config/game_fingerprints.toml"
            --ultimate-fingerprints "${CMAKE_CURRENT_SOURCE_DIR}/config/ultimate_fingerprints.toml"
            --header "${RBBLITZ_EMBEDDED_CONFIG}"
            --ispp "${RBBLITZ_ISPP_PINS}"
            ${RBBLITZ_EMBED_EXTRA_ARGS}
    DEPENDS rb_blitz_embed_config ${RBBLITZ_CONFIG_SOURCES}
    COMMENT "Embedding installer config (pins + fingerprints)"
    VERBATIM)

add_custom_target(rb_blitz_installer_config
    DEPENDS "${RBBLITZ_EMBEDDED_CONFIG}" "${RBBLITZ_ISPP_PINS}")

# --------------------------------------------------------------------------
# Helper: the work the Inno wizard cannot do in Pascal - STFS extraction, zip
# extraction, HTTPS download, file placement and verification.
# --------------------------------------------------------------------------
set(RBBLITZ_HELPER_SOURCES
    src/commands.cpp
    src/config.cpp
    src/download.cpp
    src/inflate.cpp
    src/install.cpp
    src/main.cpp
    src/stfs.cpp
    src/util.cpp
    src/zip.cpp)

add_executable(rb_blitz_setup_helper ${RBBLITZ_HELPER_SOURCES})
add_dependencies(rb_blitz_setup_helper rb_blitz_installer_config)
target_include_directories(rb_blitz_setup_helper PRIVATE
    src
    "${RBBLITZ_INSTALLER_GENERATED}"
    "${RBBLITZ_ROOT}/src")
target_compile_features(rb_blitz_setup_helper PRIVATE cxx_std_20)

# Compiled from the emulator tree rather than copied, so the installer cannot
# drift from what the runtime expects.
target_sources(rb_blitz_setup_helper PRIVATE
    "${RBBLITZ_ROOT}/src/util/sha256.cpp"
    "${RBBLITZ_ROOT}/src/util/game_fingerprint.cpp")

target_link_libraries(rb_blitz_setup_helper PRIVATE winhttp)

# The helper runs from the wizard's temporary directory on a machine that may
# not have the VC++ redistributable installed, and it is what downloads the
# payload in the online-install mode, so it cannot depend on DLLs shipped by
# that payload. Static CRT: only OS DLLs are imported.
set_property(TARGET rb_blitz_setup_helper PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

# setup.iss embeds the helper and pins.iss from out/generated, so that copy is
# refreshed by the build rather than by hand: a stale helper inside a compiled
# setup exe is invisible until the install fails on the user's machine.
set(RBBLITZ_INSTALLER_STAGE "${CMAKE_CURRENT_SOURCE_DIR}/out/generated")

add_custom_command(TARGET rb_blitz_setup_helper POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RBBLITZ_INSTALLER_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${RBBLITZ_ISPP_PINS}"
            "${RBBLITZ_INSTALLER_STAGE}/pins.iss"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:rb_blitz_setup_helper>"
            "${RBBLITZ_INSTALLER_STAGE}/rb_blitz_setup_helper.exe"
    COMMENT "Staging the helper and pins.iss for Inno Setup"
    VERBATIM)

if(MSVC)
    target_compile_options(rb_blitz_setup_helper PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(rb_blitz_setup_helper PRIVATE -Wall -Wextra)
endif()

# --------------------------------------------------------------------------
# Tests: dependency-free, same harness as the emulator tests (tests/check.h).
# --------------------------------------------------------------------------
include(CTest)

if(BUILD_TESTING)
    add_executable(rb_blitz_installer_tests
        tests/installer_tests.cpp
        src/commands.cpp
        src/config.cpp
        src/download.cpp
        src/inflate.cpp
        src/install.cpp
        src/stfs.cpp
        src/util.cpp
        src/zip.cpp)
    add_dependencies(rb_blitz_installer_tests rb_blitz_installer_config)
    target_include_directories(rb_blitz_installer_tests PRIVATE
        src
        "${RBBLITZ_INSTALLER_GENERATED}"
        "${RBBLITZ_ROOT}/src"
        "${RBBLITZ_ROOT}/tests")
    target_compile_features(rb_blitz_installer_tests PRIVATE cxx_std_20)
    target_compile_definitions(rb_blitz_installer_tests PRIVATE
        RBBLITZ_INSTALLER_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config"
        RBBLITZ_ROOT_DIR="${RBBLITZ_ROOT}")
    target_sources(rb_blitz_installer_tests PRIVATE
        "${RBBLITZ_ROOT}/src/util/sha256.cpp"
        "${RBBLITZ_ROOT}/src/util/game_fingerprint.cpp")
    target_link_libraries(rb_blitz_installer_tests PRIVATE winhttp)

    if(MSVC)
        target_compile_options(rb_blitz_installer_tests PRIVATE /W4 /utf-8 /EHsc)
    else()
        target_compile_options(rb_blitz_installer_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME installer COMMAND rb_blitz_installer_tests)
endif()
