# Standalone installer for Rock Band Blitz (see installer/README.md).
#
# This is a separate CMake project on purpose: the installer is shipped as a
# Windows executable, not as part of the emulator build, and it must build on a
# machine that has no recompiled output and no game data. It therefore compiles a
# handful of project-owned files from the emulator tree instead of reimplementing
# them: src/util/sha256.cpp and src/util/game_fingerprint.cpp (so the fingerprint
# rules the installer enforces are literally the ones the runtime enforces), and
# src/settings + src/profile (so the paths.toml the installer writes is read back
# by exactly the code the game reads it with). Every one of those is SDK-free by
# design; nothing else from the emulator tree is reachable from here.

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
# The release manifest: published as the asset `update.toml` (Controller Settings's update
# check reads it, and the updater installs the payload it names), and read by
# installer/build.ps1, which prints it in the list of what a release has to publish.
set(RBBLITZ_UPDATE_MANIFEST "${RBBLITZ_INSTALLER_GENERATED}/update.toml")

set(RBBLITZ_CONFIG_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/config/pins.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/config/ultimate_fingerprints.toml"
    "${RBBLITZ_ROOT}/config/game_fingerprints.toml")

add_custom_command(
    OUTPUT "${RBBLITZ_EMBEDDED_CONFIG}" "${RBBLITZ_ISPP_PINS}" "${RBBLITZ_UPDATE_MANIFEST}"
    COMMAND rb_blitz_embed_config
            --pins "${CMAKE_CURRENT_SOURCE_DIR}/config/pins.toml"
            --game-fingerprints "${RBBLITZ_ROOT}/config/game_fingerprints.toml"
            --ultimate-fingerprints "${CMAKE_CURRENT_SOURCE_DIR}/config/ultimate_fingerprints.toml"
            --header "${RBBLITZ_EMBEDDED_CONFIG}"
            --ispp "${RBBLITZ_ISPP_PINS}"
            --update-manifest "${RBBLITZ_UPDATE_MANIFEST}"
            ${RBBLITZ_EMBED_EXTRA_ARGS}
    DEPENDS rb_blitz_embed_config ${RBBLITZ_CONFIG_SOURCES}
    COMMENT "Embedding installer config (pins + fingerprints + update manifest)"
    VERBATIM)

add_custom_target(rb_blitz_installer_config
    DEPENDS "${RBBLITZ_EMBEDDED_CONFIG}" "${RBBLITZ_ISPP_PINS}" "${RBBLITZ_UPDATE_MANIFEST}")

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
    src/paths.cpp
    src/release_api.cpp
    src/stfs.cpp
    src/util.cpp
    src/zip.cpp)

# The game's own path file (settings/paths.h) and the profile it falls back
# to, compiled from the emulator tree rather than reimplemented, so the bytes the
# installer writes are literally the bytes the game reads (parent plan §4.4: "a
# shared SDK-free storage module for the game and installer where their ownership
# overlaps"). All of these are SDK-free by design - they build with no rexglue, no
# <windows.h> and no SDL - which is what makes the installer able to reuse them.
set(RBBLITZ_SHARED_SETTINGS_SOURCES
    "${RBBLITZ_ROOT}/src/settings/model.cpp"
    "${RBBLITZ_ROOT}/src/settings/store.cpp"
    "${RBBLITZ_ROOT}/src/settings/schema.cpp"
    "${RBBLITZ_ROOT}/src/settings/paths.cpp"
    "${RBBLITZ_ROOT}/src/profile/profile.cpp"
    "${RBBLITZ_ROOT}/src/profile/profile_path.cpp")

if(NOT MSVC)
    # The game's own sources define _CRT_SECURE_NO_WARNINGS themselves, which the
    # installer's own definition collides with, and clang 19 in C++20 mode reports one
    # -Wunused-variable the game's build does not. Silencing both for those files is
    # cheaper than carrying a fork of the game's storage module here.
    set_source_files_properties(${RBBLITZ_SHARED_SETTINGS_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable;-Wno-macro-redefined")
endif()

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
    "${RBBLITZ_ROOT}/src/util/game_fingerprint.cpp"
    ${RBBLITZ_SHARED_SETTINGS_SOURCES})

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
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${RBBLITZ_UPDATE_MANIFEST}"
            "${RBBLITZ_INSTALLER_STAGE}/update.toml"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:rb_blitz_setup_helper>"
            "${RBBLITZ_INSTALLER_STAGE}/rb_blitz_setup_helper.exe"
    COMMENT "Staging the helper, pins.iss and update.toml for Inno Setup"
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
        src/paths.cpp
        src/release_api.cpp
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
        "${RBBLITZ_ROOT}/src/util/game_fingerprint.cpp"
        ${RBBLITZ_SHARED_SETTINGS_SOURCES})
    target_link_libraries(rb_blitz_installer_tests PRIVATE winhttp)

    if(MSVC)
        target_compile_options(rb_blitz_installer_tests PRIVATE /W4 /utf-8 /EHsc)
    else()
        target_compile_options(rb_blitz_installer_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME installer COMMAND rb_blitz_installer_tests)
endif()
