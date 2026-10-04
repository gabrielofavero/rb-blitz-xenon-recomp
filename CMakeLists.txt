# rb_blitz - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(rb_blitz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # rb_blitz.rc compiles assets/blitz.ico into the executable. project() enables
    # only CXX, so the resource compiler has to be turned on explicitly before the
    # .rc is handed to a target.
    enable_language(RC)
endif()

# The path of the machine that built a payload must not travel inside it. Every
# REXLOG_*/REX_ASSERT macro expands to a __FILE__ literal, so without the mapping
# below the checkout path is embedded in all three payload binaries - measured
# before adding it: 8 hits in rb_blitz.exe, 122 in rexruntime.dll and 18 in
# rexgpu-xenos.dll, every one of them this repository's own absolute path
# (docs/distributable.md). Mapping the source and build trees to relative names
# keeps the file name useful in a log and drops the developer's disk out of it.
#
# It has to be set before generated/rexglue.cmake, which add_subdirectory()s the
# SDK: a directory property is inherited by subdirectories added after it, so the
# SDK's own libraries get the mapping too. MSVC is not covered (it needs /pathmap
# and /d1trimfile, and nothing builds the game with it here);
# scripts/audit_distributable.ps1 is what refuses a payload that leaks anyway.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
    add_compile_options("-ffile-prefix-map=${CMAKE_BINARY_DIR}=.")
endif()

include(generated/rexglue.cmake)

# A clean checkout has no generated/default/sources.cmake yet, because codegen is
# a build step, and the file above includes it only when it is already there. The
# build system of that first build therefore knows nothing about the recompiled
# code: rb_blitz.exe is linked from the hand-written sources alone and fails with
# a wall of undefined symbols (`_sub_...`, `PPCImageConfig`), which says nothing
# about the actual cause.
#
# Codegen output cannot be picked up inside the same build - Ninja only reloads
# the build system between invocations, and naming the generated file as a
# CMAKE_CONFIGURE_DEPENDS dependency does not change that (measured: the property
# is accepted and ignored for a file a custom command produces). So the configure
# has to run again, and the only thing worth engineering here is that the person
# meets that requirement instead of the linker error. The check below is added
# only to a configuration that predates the generated sources, and it runs right
# after codegen has produced them.
if(NOT REXGLUE_ENTRYPOINT_GENERATED_SOURCES)
    set(RBBLITZ_CODEGEN_CHECK "${CMAKE_CURRENT_BINARY_DIR}/rb_blitz_codegen_check.cmake")
    file(WRITE "${RBBLITZ_CODEGEN_CHECK}" [=[
if(EXISTS "${RBBLITZ_SOURCES_FILE}")
    message(FATAL_ERROR
        "Recompiled code has just been generated, but this build system was "
        "configured before it existed, so rb_blitz.exe cannot be linked in this "
        "build.\n"
        "Re-run the configure step, then build again. The second build is "
        "incremental and produces the executable:\n"
        "  cmake --preset ${RBBLITZ_PRESET}\n"
        "  cmake --build --preset ${RBBLITZ_PRESET}\n")
endif()
]=])
    string(TOLOWER "${CMAKE_BUILD_TYPE}" rbblitz_build_type)
    add_custom_command(TARGET rb_blitz_codegen POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DRBBLITZ_SOURCES_FILE=${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake"
                "-DRBBLITZ_PRESET=win-amd64-${rbblitz_build_type}"
                -P "${RBBLITZ_CODEGEN_CHECK}"
        COMMENT "Checking that this build system knows about the generated code"
        VERBATIM
    )
endif()

# Sources
set(RBBLITZ_SOURCES
    src/main.cpp
    src/diag/probe.cpp
    src/diag/probe_overlay.cpp
    src/diag/probe_points.cpp
    src/diag/probe_trace.cpp
    src/enhancements.cpp
    src/hooks/crypto.cpp
    src/hooks/dlc.cpp
    src/hooks/ultimate.cpp
    src/fs/payload_overlay.cpp
    src/input/mouse_ui.cpp
)

if(WIN32)
    # rb_blitz.rc carries the Windows resources (assets/blitz.ico) and the WIN32
    # subsystem is already selected, so the icon applies to the GUI executable.
    #
    # The resource compiler does not report the files it inlines, so bind the
    # icon to the resource object by hand: without this, swapping the .ico
    # leaves the stale icon linked in until the .rc itself changes.
    set_source_files_properties(rb_blitz.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/blitz.ico")
    add_executable(rb_blitz WIN32 ${RBBLITZ_SOURCES} rb_blitz.rc)
else()
    add_executable(rb_blitz ${RBBLITZ_SOURCES})
endif()

# rexglue_configure_target() compiles rexglue-sdk/src/ui/rex_app.cpp (and
# windowed_app_main_sdl.cpp) into this host executable. rex_app.cpp includes
# <imgui.h>, but rexruntime links imgui PRIVATE, so the vendored imgui include
# directory is not propagated through rex::runtime. Expose it to the host
# target only (headers), keeping imgui objects in rexruntime.dll where they
# already live. See docs/history/bringup-log.md B-004.
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

# Frozen toolchain
#
# config/toolchain.toml records the compiler, CMake, Ninja, MSVC toolset, Windows
# SDK and SDK pin this project is built with, and this gate compares the machine
# against it before codegen - the same shape as the game-data gate above, and for
# the same reason: the knowledge of "what works here" used to live in this build
# tree's CMakeCache.txt, which no other machine has. docs/toolchain.md is the prose
# half; src/util/toolchain_pin.h is the parser and the comparison, covered by
# tests/toolchain_tests.cpp.
#
# A component below its recorded minimum, or the SDK off its pinned commit, stops
# the build; anything else is reported and builds, because the frozen set is what
# the last acceptance run was measured with rather than a wall in front of the next
# developer. The two switches exist for the releases-and-contributors split:
# -DRBBLITZ_STRICT_TOOLCHAIN=ON refuses every deviation (a release build that wants
# the frozen set or nothing) and -DRBBLITZ_ALLOW_OTHER_TOOLCHAIN=ON accepts every
# one (report and build).
option(RBBLITZ_STRICT_TOOLCHAIN
    "Refuse to build unless the toolchain matches config/toolchain.toml exactly" OFF)
option(RBBLITZ_ALLOW_OTHER_TOOLCHAIN
    "Report a toolchain that is not the frozen one, but never fail the build" OFF)

set(RBBLITZ_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/toolchain.toml")
set(RBBLITZ_TOOLCHAIN_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/generated/toolchain_build.h")

add_executable(rb_blitz_toolchain
    tools/toolchain_check.cpp
    src/util/toolchain_pin.cpp
)
target_include_directories(rb_blitz_toolchain PRIVATE src)

set(RBBLITZ_TOOLCHAIN_PROBE_ARGS
    --project-dir "${CMAKE_CURRENT_SOURCE_DIR}"
    --cxx "${CMAKE_CXX_COMPILER}"
    --cmake "${CMAKE_COMMAND}"
    --ninja "${CMAKE_MAKE_PROGRAM}"
)
set(RBBLITZ_TOOLCHAIN_GATE_ARGS ${RBBLITZ_TOOLCHAIN_PROBE_ARGS})
if(RBBLITZ_STRICT_TOOLCHAIN)
    list(APPEND RBBLITZ_TOOLCHAIN_GATE_ARGS --strict)
endif()
if(RBBLITZ_ALLOW_OTHER_TOOLCHAIN)
    list(APPEND RBBLITZ_TOOLCHAIN_GATE_ARGS --allow-other-toolchain)
endif()

# The stamp the binary reports at boot. Generated rather than configured, because
# what it records is what the probes above found on this machine, not the record in
# the .toml - a binary built off the frozen set says so in its own log.
add_custom_command(
    OUTPUT "${RBBLITZ_TOOLCHAIN_HEADER}"
    COMMAND $<TARGET_FILE:rb_blitz_toolchain> --emit-header "${RBBLITZ_TOOLCHAIN_HEADER}"
            ${RBBLITZ_TOOLCHAIN_PROBE_ARGS}
    DEPENDS rb_blitz_toolchain "${RBBLITZ_TOOLCHAIN_FILE}"
    COMMENT "Emitting the toolchain boot stamp"
    VERBATIM
)
target_sources(rb_blitz PRIVATE "${RBBLITZ_TOOLCHAIN_HEADER}")

add_custom_target(rb_blitz_toolchain_gate
    COMMAND $<TARGET_FILE:rb_blitz_toolchain> --check ${RBBLITZ_TOOLCHAIN_GATE_ARGS}
    DEPENDS rb_blitz_toolchain
    COMMENT "Checking the toolchain against config/toolchain.toml"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(rb_blitz_codegen rb_blitz_toolchain_gate)

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

    add_executable(rb_blitz_path_policy_tests
        tests/path_policy_tests.cpp
    )
    target_include_directories(rb_blitz_path_policy_tests PRIVATE src)
    add_test(NAME path_policy COMMAND rb_blitz_path_policy_tests)

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

    add_executable(rb_blitz_ui_nav_tests
        tests/ui_nav_tests.cpp
    )
    target_include_directories(rb_blitz_ui_nav_tests PRIVATE src)
    add_test(NAME ui_nav COMMAND rb_blitz_ui_nav_tests)

    add_executable(rb_blitz_probe_trace_tests
        tests/probe_trace_tests.cpp
        src/diag/probe_trace.cpp
    )
    target_include_directories(rb_blitz_probe_trace_tests PRIVATE src)
    add_test(NAME probe_trace COMMAND rb_blitz_probe_trace_tests)

    add_executable(rb_blitz_dlc_layout_tests
        tests/dlc_layout_tests.cpp
    )
    target_include_directories(rb_blitz_dlc_layout_tests PRIVATE src)
    add_test(NAME dlc_layout COMMAND rb_blitz_dlc_layout_tests)

    add_executable(rb_blitz_ultimate_plan_tests
        tests/ultimate_plan_tests.cpp
    )
    target_include_directories(rb_blitz_ultimate_plan_tests PRIVATE src)
    add_test(NAME ultimate_plan COMMAND rb_blitz_ultimate_plan_tests)

    add_executable(rb_blitz_toolchain_tests
        tests/toolchain_tests.cpp
        src/util/toolchain_pin.cpp
    )
    target_include_directories(rb_blitz_toolchain_tests PRIVATE src)
    add_test(NAME toolchain
             COMMAND rb_blitz_toolchain_tests --project-dir "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# The launcher (docs/plans/launcher-plan.md D1): a second executable in this build tree,
# next to rb_blitz.exe, so a release always ships both. It carries its own settings
# schema, which is compiled and validated at build time.
add_subdirectory(launcher)
