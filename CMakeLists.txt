# rb_blitz - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(rb_blitz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # rb_blitz.rc compiles blitz.ico into the executable. project() enables only
    # CXX, so the resource compiler has to be turned on explicitly before the
    # .rc is handed to a target.
    enable_language(RC)
endif()

include(generated/rexglue.cmake)

# Sources
set(RBBLITZ_SOURCES
    src/main.cpp
    src/hooks/crypto.cpp
    src/hooks/ultimate.cpp
    src/fs/payload_overlay.cpp
)

if(WIN32)
    # rb_blitz.rc carries the Windows resources (blitz.ico) and the WIN32
    # subsystem is already selected, so the icon applies to the GUI executable.
    #
    # The resource compiler does not report the files it inlines, so bind the
    # icon to the resource object by hand: without this, swapping blitz.ico
    # leaves the stale icon linked in until the .rc itself changes.
    set_source_files_properties(rb_blitz.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/blitz.ico")
    add_executable(rb_blitz WIN32 ${RBBLITZ_SOURCES} rb_blitz.rc)
else()
    add_executable(rb_blitz ${RBBLITZ_SOURCES})
endif()

# rexglue_configure_target() compiles rexglue-sdk/src/ui/rex_app.cpp (and
# windowed_app_main_sdl.cpp) into this host executable. rex_app.cpp includes
# <imgui.h>, but rexruntime links imgui PRIVATE, so the vendored imgui include
# directory is not propagated through rex::runtime. Expose it to the host
# target only (headers), keeping imgui objects in rexruntime.dll where they
# already live. See docs/bringup-log.md B-004.
if(TARGET imgui)
    target_include_directories(rb_blitz PRIVATE
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
else()
    target_include_directories(rb_blitz PRIVATE
        "${REXSDK_DIR}/thirdparty/imgui")
endif()

rexglue_setup_target(rb_blitz GPU_PLUGINS xenos)

# Game fingerprint gate
#
# config/game_fingerprints.toml records the sizes and SHA-256 digests of the
# supported Rock Band Blitz dump. Recompiled code is only valid for that dump, so
# the gate below runs before codegen and fails the build when game/default.xex is
# something else - a wrong revision produces a binary that executes addresses
# which do not exist locally, and that failure is much harder to read than this
# one. Two consumers, one parser: this gate, and the boot identity line in
# src/rb_blitz_app.h that logs which revision the running build was made for.
# The gate checks the codegen input only; the 360 MB archive rows are audited on
# demand with `rb_blitz_fingerprint --all` (docs/build-and-run.md).
#
# A deliberately modified content root (Rock Band Blitz Ultimate, see
# docs/ultimate-compat.md) is a supported configuration, so it can be allowed:
# codegen still runs on whatever local default.xex is present, but the mismatch is
# then reported instead of fatal.
option(RBBLITZ_ALLOW_MODIFIED_GAME_DATA
    "Allow building when game/ is not the dump config/game_fingerprints.toml describes" OFF)

set(RBBLITZ_FINGERPRINT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/game_fingerprints.toml")
set(RBBLITZ_GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/game")
set(RBBLITZ_EXPECTED_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/generated/fingerprint_expected.h")

add_executable(rb_blitz_fingerprint
    tools/fingerprint_check.cpp
    src/util/sha256.cpp
    src/util/game_fingerprint.cpp
)
target_include_directories(rb_blitz_fingerprint PRIVATE src)

# The header is generated, so it is built rather than committed; it lands in
# generated/, which is ignored apart from generated/rexglue.cmake.
add_custom_command(
    OUTPUT "${RBBLITZ_EXPECTED_HEADER}"
    COMMAND $<TARGET_FILE:rb_blitz_fingerprint>
            --fingerprints "${RBBLITZ_FINGERPRINT_FILE}"
            --emit-header "${RBBLITZ_EXPECTED_HEADER}"
            --quiet
    DEPENDS rb_blitz_fingerprint "${RBBLITZ_FINGERPRINT_FILE}"
    COMMENT "Emitting expected game fingerprint header"
    VERBATIM
)

# src/rb_blitz_app.h includes the header above to report the boot identity, so
# both the source and the generated header belong to the host target. Listing the
# header as a source is what makes CMake generate it before compiling.
target_sources(rb_blitz PRIVATE
    src/util/sha256.cpp
    "${RBBLITZ_EXPECTED_HEADER}"
)

set(RBBLITZ_FINGERPRINT_GATE_ARGS
    --fingerprints "${RBBLITZ_FINGERPRINT_FILE}"
    --root "${RBBLITZ_GAME_DIR}"
    --check
)
if(RBBLITZ_ALLOW_MODIFIED_GAME_DATA)
    list(APPEND RBBLITZ_FINGERPRINT_GATE_ARGS --allow-mismatch)
endif()

add_custom_target(rb_blitz_fingerprint_gate
    COMMAND $<TARGET_FILE:rb_blitz_fingerprint> ${RBBLITZ_FINGERPRINT_GATE_ARGS}
    DEPENDS rb_blitz_fingerprint
    COMMENT "Checking game data against config/game_fingerprints.toml"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(rb_blitz_codegen rb_blitz_fingerprint_gate)

# Host unit tests. Dependency-free (tests/check.h) because the SDK vendors catch2
# as an uninitialised submodule, and SDK-free so they run without booting the game
# - see docs/build-and-run.md. `cmake --build <build dir>; ctest` runs them.
include(CTest)

if(BUILD_TESTING)
    add_executable(rb_blitz_crypto_keytable_tests
        tests/crypto_keytable_tests.cpp
    )
    target_include_directories(rb_blitz_crypto_keytable_tests PRIVATE src)
    add_test(NAME crypto_keytable COMMAND rb_blitz_crypto_keytable_tests)

    add_executable(rb_blitz_payload_overlay_tests
        tests/payload_overlay_tests.cpp
    )
    target_include_directories(rb_blitz_payload_overlay_tests PRIVATE src)
    add_test(NAME payload_overlay COMMAND rb_blitz_payload_overlay_tests)

    add_executable(rb_blitz_fingerprint_tests
        tests/fingerprint_tests.cpp
        src/util/sha256.cpp
        src/util/game_fingerprint.cpp
    )
    target_include_directories(rb_blitz_fingerprint_tests PRIVATE src)

    set(RBBLITZ_FINGERPRINT_TEST_ARGS --project-dir "${CMAKE_CURRENT_SOURCE_DIR}")
    if(RBBLITZ_ALLOW_MODIFIED_GAME_DATA)
        list(APPEND RBBLITZ_FINGERPRINT_TEST_ARGS --allow-mismatch)
    endif()
    add_test(NAME fingerprint
             COMMAND rb_blitz_fingerprint_tests ${RBBLITZ_FINGERPRINT_TEST_ARGS})
endif()
