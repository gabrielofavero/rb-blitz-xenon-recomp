# rb_blitz - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(rb_blitz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # rb_blitz.rc compiles assets/icon/blitz.ico into the executable. project() enables
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
    src/hooks/content_progress.cpp
    src/hooks/crypto.cpp
    src/hooks/dlc.cpp
    src/hooks/dlc_refresh.cpp
    src/hooks/local_save.cpp
    src/hooks/menu_filter.cpp
    src/hooks/settings_bridge.cpp
    src/hooks/settings_bridge_hooks.cpp
    src/hooks/render_settings.cpp
    src/hooks/settings_payload.cpp
    src/settings/menu_payload.cpp
    src/ui/settings_screen.cpp
    # P9's runtime half of the live apply: the one place that knows how a `hot` row reaches the
    # running game (today `graphics.vsync` -> the SDK's own `vsync`, the call R7 already measured).
    src/hooks/hot_apply_runtime.cpp
    src/hooks/shell_music.cpp
    src/hooks/ultimate.cpp
    src/fs/payload_overlay.cpp
    src/input/remap.cpp
    src/input/mouse_ui.cpp
    src/launcher/profile.cpp
    src/launcher/profile_apply.cpp
    src/launcher/profile_path.cpp
    src/launcher/remap.cpp
    # P6's addition to the coordinator-owned list: the Ultimate provider adapter (the ark index
    # reader, the shape check over the payload's own definitions, the row bindings and the
    # provider's settings file). SDK-free.
    src/settings/ultimate_adapter.cpp
    # P11a's addition: the rest of the store, now that a boot consumes it. The service, the store,
    # the path file and the migration (P3), the capability/policy pair (P4), the service-backed
    # guest sink and the boot wiring itself. All SDK-free except src/settings/boot.cpp, which is the
    # game's half (it reads and writes cvars).
    src/settings/store.cpp
    src/settings/paths.cpp
    src/settings/migration.cpp
    src/settings/service.cpp
    src/settings/cvars.cpp
    src/settings/capabilities.cpp
    src/settings/graphics_policy.cpp
    src/settings/bridge_sink.cpp
    src/settings/boot.cpp
    src/settings/graphics_boot.cpp
    # P9's two additions: the application-strategy seam (§5.5 - which committed rows apply to the
    # running game and which wait for the restart prompt) and the restart coordinator (§8 - the
    # exactly-once request, the rebuilt command line and the bootstrap's verified-identity wait).
    # SDK-free. The seam reads the §5.5 table out of the schema, so the model and the schema come
    # with it; the restart coordinator's caller is the boot wiring, which is the coordinator's.
    src/settings/model.cpp
    src/settings/schema.cpp
    src/settings/hot_apply.cpp
    # P10's menu-compatibility rules (the hide matrix §3.5 states in words, the fixed 50-song
    # pool, the rename's scope). SDK-free, and read by the menu hooks the mod already owns.
    src/settings/menu_compat.cpp
    src/platform/game_restart.cpp
    # The compiled-DTA parser, promoted out of the test-only targets: the provider adapter reads
    # the payload's own definitions with it at boot (it was P1's authoring module, with no
    # production consumer until P6). The cipher it shares with menu_options.cpp is already here.
    src/ui/dtb_author.cpp
    src/ui/menu_options.cpp
)

if(WIN32)
    # rb_blitz.rc carries the Windows resources (assets/icon/blitz.ico) and the WIN32
    # subsystem is already selected, so the icon applies to the GUI executable.
    #
    # The resource compiler does not report the files it inlines, so bind the
    # icon to the resource object by hand: without this, swapping the .ico
    # leaves the stale icon linked in until the .rc itself changes.
    set_source_files_properties(rb_blitz.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/blitz.ico")
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
    add_executable(rb_blitz_local_save_tests tests/local_save_tests.cpp src/util/sha256.cpp)
    target_include_directories(rb_blitz_local_save_tests PRIVATE src)
    add_test(NAME local_save COMMAND rb_blitz_local_save_tests)

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

    # The flat DLC library (src/fs/dlc_library.h): what a package says it is, what a
    # recursive walk of a dumped song folder finds, and the names the guest can carry
    # back. SDK-free like the layout tests above, and for the same reason.
    add_executable(rb_blitz_dlc_library_tests
        tests/dlc_library_tests.cpp
    )
    target_include_directories(rb_blitz_dlc_library_tests PRIVATE src)
    add_test(NAME dlc_library COMMAND rb_blitz_dlc_library_tests)

    # The R7 enumeration cache (src/fs/dlc_cache.h): the fingerprint, the round trip and
    # the ways a cache is refused. SDK-free like the two DLC tests above, and for the
    # same reason - the decision is host logic and runs without booting the game.
    add_executable(rb_blitz_dlc_cache_tests
        tests/dlc_cache_tests.cpp
    )
    target_include_directories(rb_blitz_dlc_cache_tests PRIVATE src)
    add_test(NAME dlc_cache COMMAND rb_blitz_dlc_cache_tests)

    # The in-game refresh's two rules (src/fs/refresh_state.h): one refresh at a time, and
    # a finished one published exactly once. SDK-free and thread-free like the DLC tests
    # above; the caller owns the threads, so the state machine is what is testable.
    add_executable(rb_blitz_refresh_state_tests
        tests/refresh_state_tests.cpp
    )
    target_include_directories(rb_blitz_refresh_state_tests PRIVATE src)
    add_test(NAME refresh_state COMMAND rb_blitz_refresh_state_tests)

    # The typed guest<->host settings channel (src/hooks/settings_bridge.h, P1): exact namespace
    # matching, bounded and checksummed frames, structured status and idempotent request ids.
    # SDK-free like the tests above; the guest hook that carries the frames needs the SDK and a
    # boot, and is deliberately not here.
    add_executable(rb_blitz_settings_bridge_tests
        tests/settings_bridge_tests.cpp
        src/hooks/settings_bridge.cpp
    )
    target_include_directories(rb_blitz_settings_bridge_tests PRIVATE src)
    add_test(NAME settings_bridge COMMAND rb_blitz_settings_bridge_tests)

    # The authoring side of the compiled-DTA grammar (src/ui/dtb_author.h, P1): produce a
    # definition of our own rather than edit one the title already has. SDK-free, and it reuses
    # menu_options::EncodeFile, so the test links that module too. The real-file case is opt-in:
    # pass a .dtb path as argv[1] to check the grammar against actual content; the CMake test
    # runs without one, so no retail asset is ever needed.
    add_executable(rb_blitz_dtb_author_tests
        tests/dtb_author_tests.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
    )
    target_include_directories(rb_blitz_dtb_author_tests PRIVATE src)
    add_test(NAME dtb_author COMMAND rb_blitz_dtb_author_tests)

    # P1's native screen (src/ui/settings_screen.h): the authored Advanced Settings definition, the
    # two edits that make the retail main menu reach it, and the request paths it writes - checked
    # against the bridge's own text grammar, which is what a DTA script spells as a literal.
    # SDK-free; the fixtures are synthetic retail shapes, so no retail content is needed.
    add_executable(rb_blitz_settings_screen_tests
        tests/settings_screen_tests.cpp
        src/ui/settings_screen.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
        src/hooks/settings_bridge.cpp
    )
    target_include_directories(rb_blitz_settings_screen_tests PRIVATE src)
    add_test(NAME settings_screen COMMAND rb_blitz_settings_screen_tests)

    # The archive half of the generated Help & Options payload (src/settings/menu_payload.cpp):
    # that extending the mounted patch archive keeps it a *one-part* archive, because the title has
    # never read a two-part one and a boot that offered it one hung inside our own read hook
    # (docs/plans/in-game-settings-status-2026-10-12.md). The assertions run through
    # src/settings/ultimate_adapter.cpp's own v6 reader, so the fixture and the product cannot drift
    # apart. SDK-free: the fixtures are an index and bytes, no retail content.
    add_executable(rb_blitz_menu_payload_tests
        tests/menu_payload_tests.cpp
        src/settings/menu_payload.cpp
        src/settings/ultimate_adapter.cpp
        src/ui/settings_screen.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
    )
    target_include_directories(rb_blitz_menu_payload_tests PRIVATE src)
    add_test(NAME menu_payload COMMAND rb_blitz_menu_payload_tests)

    # R9's pool and deck (src/hooks/menu_music_pool.h): that a merged pack's songs are all
    # pooled and sampled at random, and that no song is ever dealt twice in a row. The hook
    # that uses it (src/hooks/shell_music.cpp) needs the SDK and a boot; this half does not.
    add_executable(rb_blitz_menu_music_pool_tests
        tests/menu_music_pool_tests.cpp
    )
    target_include_directories(rb_blitz_menu_music_pool_tests PRIVATE src)
    add_test(NAME menu_music_pool COMMAND rb_blitz_menu_music_pool_tests)

    # The menu filter's edit (src/ui/menu_options.cpp): the compiled DTA grammar, the
    # byte-neutral rewrite and the title's SHA-1, run against a synthetic fixture built
    # in the test itself - no boot, and no retail content (D14).
    add_executable(rb_blitz_menu_options_tests
        tests/menu_options_tests.cpp
        src/ui/menu_options.cpp
    )
    target_include_directories(rb_blitz_menu_options_tests PRIVATE src)
    add_test(NAME menu_options COMMAND rb_blitz_menu_options_tests)

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

    # The launcher profile: path resolution and the reader/writer (P0.3). SDK-free, so it
    # runs without booting anything, and compiled from src/ because the game executable
    # reads the same file (D1).
    add_executable(rb_blitz_launcher_profile_tests
        tests/launcher_profile_tests.cpp
        src/launcher/profile.cpp
        src/launcher/profile_path.cpp
    )
    target_include_directories(rb_blitz_launcher_profile_tests PRIVATE src)
    add_test(NAME launcher_profile COMMAND rb_blitz_launcher_profile_tests)

    # The General tab's decidable half (B1): D5's four Ultimate states from directory
    # fixtures, the launch-target fallback, where the game is detected, and the schema's
    # `validate` rules. Dependency-free like the profile tests, and compiled from
    # launcher/src because the launcher is what puts them on screen.
    add_executable(rb_blitz_launcher_general_tests
        tests/launcher_ultimate_state_tests.cpp
        launcher/src/path_validate.cpp
        launcher/src/ultimate_state.cpp
    )
    target_include_directories(rb_blitz_launcher_general_tests PRIVATE src launcher/src)
    add_test(NAME launcher_general COMMAND rb_blitz_launcher_general_tests)

    # B4's write path: the session on top of the profile module, plus the reader for the game's
    # own file that the precedence badge is decided from.
    add_executable(rb_blitz_launcher_session_tests
        tests/launcher_profile_session_tests.cpp
        launcher/src/game_config.cpp
        launcher/src/prefill.cpp
        launcher/src/profile_session.cpp
        src/launcher/profile.cpp
        src/launcher/profile_path.cpp
    )
    target_include_directories(rb_blitz_launcher_session_tests PRIVATE src launcher/src)
    add_test(NAME launcher_session COMMAND rb_blitz_launcher_session_tests)

    # D16's vocabulary: the `[remap]` grammar, what the profile round-trips, and the rewrite
    # itself. Dependency-free like the profile tests - no pad, no window, no SDL - because the
    # device side is only the callback the game hands the rewrite (src/input/remap.cpp).
    add_executable(rb_blitz_launcher_remap_tests
        tests/launcher_remap_tests.cpp
        src/launcher/profile.cpp
        src/launcher/remap.cpp
    )
    target_include_directories(rb_blitz_launcher_remap_tests PRIVATE src)
    add_test(NAME launcher_remap COMMAND rb_blitz_launcher_remap_tests)

    # D19's update check: the release manifest, the version comparison, and the two decisions
    # that come out of them (is there an update, and should the user be asked). Dependency-free
    # like the profile tests - the fetch and the updater are launcher/src/update_launcher.cpp,
    # which is the platform half and is not compiled here, so every rule is decided from text.
    add_executable(rb_blitz_launcher_update_tests
        tests/launcher_update_tests.cpp
        launcher/src/update_check.cpp
    )
    target_include_directories(rb_blitz_launcher_update_tests PRIVATE launcher/src)
    add_test(NAME launcher_update COMMAND rb_blitz_launcher_update_tests)

    # P3 (docs/plans/in-game-settings-contracts.md §5): the SDK-free settings store. The
    # comment-preserving TOML editor, the typed document, atomic replace, and the conflict
    # rule that never lets a second writer clobber a hand edit. No SDK, no boot.
    add_executable(rb_blitz_settings_store_tests
        tests/settings_store_tests.cpp
        src/settings/model.cpp
        src/settings/schema.cpp
        src/settings/store.cpp
    )
    target_include_directories(rb_blitz_settings_store_tests PRIVATE src)
    add_test(NAME settings_store COMMAND rb_blitz_settings_store_tests)

    # P3: the path file and the legacy migration. Compiled from src/ because the game reads
    # the same files, and from src/launcher because the migration consumes the profile model.
    add_executable(rb_blitz_settings_migration_tests
        tests/settings_migration_tests.cpp
        src/settings/model.cpp
        src/settings/schema.cpp
        src/settings/store.cpp
        src/settings/paths.cpp
        src/settings/migration.cpp
        src/launcher/profile.cpp
        src/launcher/profile_path.cpp
    )
    target_include_directories(rb_blitz_settings_migration_tests PRIVATE src)
    add_test(NAME settings_migration COMMAND rb_blitz_settings_migration_tests)

    # P3: the settings service. Requested/effective/pending, revisioned transactions, the
    # durable commit, the provider save seam and the restart handshake. No SDK, no boot.
    add_executable(rb_blitz_settings_service_tests
        tests/settings_service_tests.cpp
        src/settings/model.cpp
        src/settings/schema.cpp
        src/settings/store.cpp
        src/settings/paths.cpp
        src/settings/migration.cpp
        src/settings/cvars.cpp
        src/settings/service.cpp
        src/launcher/profile.cpp
        src/launcher/profile_path.cpp
    )
    target_include_directories(rb_blitz_settings_service_tests PRIVATE src)
    add_test(NAME settings_service COMMAND rb_blitz_settings_service_tests)

    # P4: graphics capabilities and the graphics policy. The same SDK-free settings modules the
    # service test links (the policy implements the service's Reconciler interface), plus the
    # two P4 files. No SDK, no boot, no GPU: the probe results are plain data.
    add_executable(rb_blitz_settings_graphics_tests
        tests/settings_graphics_tests.cpp
        src/settings/graphics_boot.cpp
        src/settings/model.cpp
        src/settings/schema.cpp
        src/settings/store.cpp
        src/settings/paths.cpp
        src/settings/migration.cpp
        src/settings/cvars.cpp
        src/settings/service.cpp
        src/settings/capabilities.cpp
        src/settings/graphics_policy.cpp
        src/launcher/profile.cpp
        src/launcher/profile_path.cpp
    )
    target_include_directories(rb_blitz_settings_graphics_tests PRIVATE src)
    add_test(NAME settings_graphics COMMAND rb_blitz_settings_graphics_tests)

    # P6: the Ultimate provider adapter (src/settings/ultimate_adapter.h). SDK-free: the ark
    # index reader, the compiled-DTA shape check over the provider's own definitions, the frozen
    # row bindings and the provider's settings-file reader/writer. Links the DTB parser for the
    # shape check. Its real-installation leg is opt-in - pass the payload root as argv[1] -
    # because the payload is local, gitignored content; the hermetic leg authors a fixture with
    # the same builder.
    add_executable(rb_blitz_ultimate_adapter_tests
        tests/ultimate_adapter_tests.cpp
        src/settings/ultimate_adapter.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
    )
    target_include_directories(rb_blitz_ultimate_adapter_tests PRIVATE src)
    add_test(NAME ultimate_adapter COMMAND rb_blitz_ultimate_adapter_tests)
    # The same binary, run against the installed payload, is the fidelity check for the fixture.
    # Registered only when that payload is actually in the checkout: `game/` is local, gitignored
    # content, so an unconfigured clone must not gain a test that cannot pass.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/ultimate/gen/patch_xbox.hdr")
        add_test(NAME ultimate_adapter_payload
                 COMMAND rb_blitz_ultimate_adapter_tests
                         "${CMAKE_CURRENT_SOURCE_DIR}/game/ultimate")
    endif()

    # P9's application-strategy seam (src/settings/hot_apply.h). SDK-free: it links the schema
    # (the §5.5 table), the model, the seam itself, and the provider adapter - the last one so the
    # test can assert the schema's strategy table and the provider's own row table agree instead of
    # drifting apart.
    add_executable(rb_blitz_settings_hot_apply_tests
        tests/settings_hot_apply_tests.cpp
        src/settings/model.cpp
        src/settings/schema.cpp
        src/settings/hot_apply.cpp
        src/settings/ultimate_adapter.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
    )
    target_include_directories(rb_blitz_settings_hot_apply_tests PRIVATE src)
    add_test(NAME settings_hot_apply COMMAND rb_blitz_settings_hot_apply_tests)

    # P9's restart coordinator (src/platform/game_restart.h). SDK-free and dependency-free: the
    # token, the argument rebuild and the state machine are pure, and the process half runs for
    # real - the test spawns *itself* as the child, so the wait-for-the-parent's-verified-identity
    # behaviour is measured against live processes rather than mocked.
    add_executable(rb_blitz_game_restart_tests
        tests/game_restart_tests.cpp
        src/platform/game_restart.cpp
    )
    target_include_directories(rb_blitz_game_restart_tests PRIVATE src)
    add_test(NAME game_restart COMMAND rb_blitz_game_restart_tests)

    # P11a: the guest channel's real store (src/settings/bridge_sink.h) - the same frames and ops
    # P1's in-memory sink answers, with the native rows going through P3's service instead. SDK-free
    # and run against a real service over a temporary directory, so a full
    # edit -> set -> commit -> read-back -> ack cycle is checked against bytes on disk rather than a
    # mock.
    add_executable(rb_blitz_settings_bridge_service_tests
        tests/settings_bridge_service_tests.cpp
        src/settings/model.cpp
        src/settings/schema.cpp
        src/settings/store.cpp
        src/settings/paths.cpp
        src/settings/migration.cpp
        src/settings/cvars.cpp
        src/settings/service.cpp
        src/settings/bridge_sink.cpp
        src/hooks/settings_bridge.cpp
        src/launcher/profile.cpp
        src/launcher/profile_path.cpp
    )
    target_include_directories(rb_blitz_settings_bridge_service_tests PRIVATE src)
    add_test(NAME settings_bridge_service COMMAND rb_blitz_settings_bridge_service_tests)

    # P10's menu-compatibility rules (src/settings/menu_compat.h). SDK-free: the hide matrix, the
    # fixed pool, the retired pool-size value and the rename's scope, plus the hide list's own seam
    # (the schema's menu-row choices, where an empty selection differs from no selection at all).
    add_executable(rb_blitz_menu_compat_tests
        tests/menu_compat_tests.cpp
        src/settings/model.cpp
        src/settings/schema.cpp
        src/settings/menu_compat.cpp
    )
    target_include_directories(rb_blitz_menu_compat_tests PRIVATE src)
    add_test(NAME menu_compat COMMAND rb_blitz_menu_compat_tests)
endif()

# The launcher (docs/plans/launcher-plan.md D1): a second executable in this build tree,
# next to rb_blitz.exe, so a release always ships both. It carries its own settings
# schema, which is compiled and validated at build time.
add_subdirectory(launcher)
