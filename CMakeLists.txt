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
# (docs/guides/distributable.md). Mapping the source and build trees to relative names
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

# Include Vulkan in dev and shipping builds, including configurations made without a preset.
# The SDK's Windows default is D3D12-only.
option(REXGLUE_USE_VULKAN "Enable Vulkan graphics backend" ON)

# The SDK's thirdparty configuration forces BUILD_TESTING=OFF for its dependencies.
# Preserve an explicit parent request so our host diagnostics/tests can still build.
if(DEFINED BUILD_TESTING)
    set(RBBLITZ_REQUESTED_BUILD_TESTING "${BUILD_TESTING}")
endif()
include(generated/rexglue.cmake)
if(DEFINED RBBLITZ_REQUESTED_BUILD_TESTING)
    set(BUILD_TESTING "${RBBLITZ_REQUESTED_BUILD_TESTING}" CACHE BOOL "Build host tests" FORCE)
endif()

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
    src/diag/gameplay_probe.cpp
    src/diag/shader_trace.cpp
    src/enhancements.cpp
    src/hooks/content_progress.cpp
    src/hooks/crypto.cpp
    src/hooks/dlc.cpp
    src/hooks/dlc_refresh.cpp
    src/hooks/hud_layout.cpp
    src/hooks/hud_layout_runtime.cpp
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
    src/hooks/menu_music_metadata.cpp
    src/hooks/ultimate.cpp
    src/fs/payload_overlay.cpp
    src/input/remap.cpp
    src/input/mouse_ui.cpp
    src/input/native_menu.cpp
    # P3 of the gameplay-mapping plan: the guest adapter that dispatches a gameplay table's
    # actions through the title's own validated boundary (the controller factory/destructor, the
    # per-frame seam and the press/release boundary), and the pad overlay that bypasses the native
    # preset without blanking input the menus, pause and calibration still need.
    src/hooks/gameplay_input.cpp
    # P2 of the gameplay-mapping plan: the source gates (SDK-free rules) and the title's own input
    # system (the SDK's drivers with the MnK emulation behind the keyboard gate, plus the physical
    # pad sampling a `pad:` gameplay source reads instead of the merged state).
    src/input/source_gates.cpp
    src/input/title_input.cpp
    # P7 of the gameplay-mapping plan: the in-game Controls store. The effective snapshot the poll
    # reads (published again after every commit), the capture state machine and its host devices,
    # the store that owns the controller profile's binding rows, and the token grammar the
    # generated menu, the sandbox and the tests share.
    src/input/effective_input.cpp
    src/input/host_pad.cpp
    src/input/input_capture.cpp
    src/input/input_store.cpp
    src/profile/input_rows.cpp
    src/profile/profile.cpp
    src/profile/profile_apply.cpp
    src/profile/profile_path.cpp
    src/profile/remap.cpp
    # P1 of the gameplay-mapping plan: the two gameplay action tables (SDK-free, shared with
    # Controller Settings so the two processes cannot disagree about what a row means).
    src/profile/gameplay_remap.cpp
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
    # P2's addition: the game window's own size (fit the decorated window into the display's usable
    # area without changing its aspect, and the logical/physical conversion the SDK's window cvars
    # are expressed in). SDK-free; the boot is the caller and the probe is Win32, like ProbeDisplay.
    src/settings/window_policy.cpp
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
    # The help captions (owner request, 2026-10-13): the retail-page text resolver the observer
    # reads (SDK-free) and the two halves that feed it - the per-frame guest-UI observer, and the
    # ImGui overlay the captions are drawn on.
    src/ui/help_topics.cpp
    src/ui/help_overlay.cpp
    src/hooks/help_observer.cpp
    # P0 of the RhythmVerse store plan: the catalogue client and its URL/byte policy (SDK-free) plus
    # the WinHTTP transport seam the store's worker threads will use. Nothing in the game calls the
    # transport yet; P0's proof is the hermetic `store_client` suite plus its manual `--live-list` arm.
    src/store/rhythmverse_client.cpp
    # P6: the store's settings as its workers read them (an immutable snapshot) and the hot applier's
    # row -> snapshot mapping. SDK-free; the runtime applier is src/hooks/hot_apply_runtime.cpp.
    src/store/store_settings.cpp
    # P2's data-layer instrument: the `netcache_init` rewrite that points the retail store at a
    # local (host-file) server (SDK-free, so its own suite covers it), the path probe that names
    # the store records the guest resolves and opens, and the request model + front-menu writer
    # skeleton the chosen seam (A) is built on.
    src/store/store_payload.cpp
    src/store/store_probe.cpp
    src/store/store_metadata.cpp
    # P3's data lane: the store's own records are answered from the file cache's loaded-file
    # container (the measured seam), so the policy that decides *what* to serve is SDK-free
    # (store_serve.cpp) while the guest-side half that inserts it is not (store_fetch.cpp).
    src/store/store_serve.cpp
    src/store/store_fetch.cpp
    # P5: the download pipeline's policy core (SDK-free), the staging/commit rules and the job
    # service behind the `rbbz-store/download/` bridge commands. The streaming transport is
    # Windows-only and appended below, like store_http.cpp.
    src/store/store_download.cpp
    src/store/store_install.cpp
    src/store/store_download_service.cpp
    # P4: the search's field, request and answer (SDK-free) plus the worker that performs it.
    # The worker is Windows-only (it includes store_http.h) and is appended below.
    src/store/store_search.cpp
    # P7's polish: the in-memory page cache both workers share (TTL, bound, single flight) and the
    # sentences the caption strip draws about the store (credit, loading, empty, offline). Both are
    # SDK-free, so their suites walk them without a boot or a socket.
    src/store/store_page_cache.cpp
    src/store/store_catalogue_status.cpp
    # P3's Downloaded mark (plan §3.2): the SDK-free (artist, title) index, the builder that reads
    # every loaded package's songs.dta, and the shared package reader the menu-music pool also uses.
    src/store/store_library_index.cpp
    src/store/store_library.cpp
    src/fs/dlc_song_metadata.cpp
    # The engine's platform texture container and its DXT blocks (src/assets/texture_codec.h):
    # the store's album art encodes with it now, and the button-layout plan's PS3 import and slot
    # sheets decode with it next. SDK-free, so the suites cover it without a boot.
    src/assets/texture_codec.cpp
    # P3's album art, host half: the key/path/URL rules and the texture cache the guest's art
    # request reads. The fetch and the image decode are Windows-only and appended below.
    src/store/album_art.cpp
)

if(WIN32)
    # WinHTTP lives behind one file so the policy stays testable without a network. Windows Imaging
    # Component decodes the cover the API serves (src/store/album_art_service.cpp is the only file
    # that uses it), which is why `windowscodecs`/`ole32` are linked below.
    list(APPEND RBBLITZ_SOURCES src/store/store_http.cpp src/store/store_transfer_winhttp.cpp
         src/store/store_search_service.cpp src/store/store_catalogue.cpp
         src/store/album_art_service.cpp)
endif()

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

# The SDK stages runtime-loaded plugins only in the executable's POST_BUILD
# step. A renderer-only rebuild need not relink the executable, leaving an old
# DLL beside it. Make staging a dependency of every game build as well.
add_custom_target(rb_blitz_stage_gpu
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:rb_blitz>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rex::gpu-xenos>" "$<TARGET_FILE_DIR:rb_blitz>"
    DEPENDS rex::gpu-xenos
    COMMENT "Staging the current GPU plugin beside rb_blitz"
    VERBATIM)
add_dependencies(rb_blitz rb_blitz_stage_gpu)

# P0 of the RhythmVerse store plan: its JSON dependency is the copy the SDK tree already vendors
# (inja's single-header nlohmann 3.11.3, MIT) - no new third-party code is added to the project - and
# the transport links WinHTTP (src/store/store_http.cpp is the only file that includes it).
target_include_directories(rb_blitz PRIVATE
    "${REXSDK_DIR}/thirdparty/inja/third_party/include")
if(WIN32)
    target_link_libraries(rb_blitz PRIVATE winhttp windowscodecs ole32)
endif()

# The store's fault logger (src/store/store_probe.cpp) prints host frames, which are resolved
# against `rb_blitz.map` by out/_swap/fault_lookup.py - and the map has to come from the same link
# as the binary or the symbols are noise (measured 2026-10-22: a map one build old named nothing
# real). Off by default; a measurement boot asks for it:
#   cmake --preset win-amd64-release -DRBBLITZ_LINK_MAP=ON
#   cmake --build --preset win-amd64-release --target rb_blitz
if(RBBLITZ_LINK_MAP)
    target_link_options(rb_blitz PRIVATE -Xlinker /MAP)
endif()

# Which graphics backends the GPU plugin was compiled with, in the order the in-game *Renderer*
# row should offer them. The same two variables decide what `rexgpu-xenos.dll` contains (the SDK
# reads them itself), so passing them on as one string keeps the two in step: the row can never
# offer a backend the plugin does not have, and it never hides one it does. The boot reads this in
# src/settings/boot.cpp (every backend gets a capability probe of its own).
set(RBBLITZ_BACKENDS "")
if(REXGLUE_USE_D3D12)
    list(APPEND RBBLITZ_BACKENDS d3d12)
endif()
if(REXGLUE_USE_VULKAN)
    list(APPEND RBBLITZ_BACKENDS vulkan)
endif()
string(REPLACE ";" "," RBBLITZ_BACKENDS_ARG "${RBBLITZ_BACKENDS}")
target_compile_definitions(rb_blitz PRIVATE "RBBLITZ_BACKENDS=\"${RBBLITZ_BACKENDS_ARG}\"")
message(STATUS "rb_blitz: renderer row will offer ${RBBLITZ_BACKENDS_ARG}")

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
# demand with `rb_blitz_fingerprint --all` (docs/guides/build-and-run.md).
#
# A deliberately modified content root (Rock Band Blitz Ultimate, see
# docs/guides/ultimate-compat.md) is a supported configuration, so it can be allowed:
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
# tree's CMakeCache.txt, which no other machine has. docs/guides/toolchain.md is the prose
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
# - see docs/guides/build-and-run.md. `cmake --build <build dir>; ctest` runs them.
include(CTest)

# P2's offline local-asset resolver hashes guest bytes with the same SDK XXH3
# implementation as both graphics backends. It never launches the guest.
add_executable(rb_blitz_shader_microcode_hash tools/shader_microcode_hash.cpp)
target_link_libraries(rb_blitz_shader_microcode_hash PRIVATE xxhash)

if(BUILD_TESTING)
    find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)
    add_test(NAME shader_asset_resolver
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_asset_resolver_tests.py"
                --hash-helper $<TARGET_FILE:rb_blitz_shader_microcode_hash>)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/default.xex" AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/gen/main_xbox.hdr")
        add_test(NAME shader_asset_resolver_retail
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_asset_resolver_tests.py"
                    --hash-helper $<TARGET_FILE:rb_blitz_shader_microcode_hash>
                    --installation "${CMAKE_CURRENT_SOURCE_DIR}/game")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/ultimate/gen/patch_xbox.hdr")
            add_test(NAME shader_asset_resolver_ultimate
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_asset_resolver_tests.py"
                        --hash-helper $<TARGET_FILE:rb_blitz_shader_microcode_hash>
                        --installation "${CMAKE_CURRENT_SOURCE_DIR}/game"
                        --effective-overlay-root "${CMAKE_CURRENT_SOURCE_DIR}/game/ultimate")
        endif()
    endif()
    # P3's offline pipeline recipe catalogue: reads the renderer's normalized
    # .xpso stores, validates every record with the same SDK XXH3 helper,
    # deduplicates/merges them with provenance and checks the referenced shader
    # identities against P2's resolver or a resolver index.
    add_test(NAME shader_pipeline_catalogue
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_pipeline_catalogue_tests.py"
                --hash-helper $<TARGET_FILE:rb_blitz_shader_microcode_hash>)
    add_executable(rb_blitz_loading_record_inspect tools/loading_record_inspect.cpp
        src/ui/dtb_author.cpp src/ui/menu_options.cpp)
    target_include_directories(rb_blitz_loading_record_inspect PRIVATE src)

    add_executable(rb_blitz_shader_compile_trace_tests tests/shader_compile_trace_tests.cpp)
    target_link_libraries(rb_blitz_shader_compile_trace_tests PRIVATE rex::runtime)
    if(WIN32)
        add_custom_command(TARGET rb_blitz_shader_compile_trace_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:rexruntime> $<TARGET_FILE_DIR:rb_blitz_shader_compile_trace_tests>)
    endif()
    add_test(NAME shader_compile_trace_enabled COMMAND rb_blitz_shader_compile_trace_tests enabled)
    add_test(NAME shader_compile_trace_disabled COMMAND rb_blitz_shader_compile_trace_tests disabled)

    # Shader-storage preload accounting and the blocking barrier, the component
    # both backends use (rexglue-sdk/include/rex/graphics/pipeline_preload.h).
    # Header-only, and exercised with real threads rather than a copied model.
    add_executable(rb_blitz_pipeline_preload_tests tests/pipeline_preload_tests.cpp)
    target_link_libraries(rb_blitz_pipeline_preload_tests PRIVATE rex::core)
    if(WIN32)
        target_link_libraries(rb_blitz_pipeline_preload_tests PRIVATE ws2_32)
    endif()
    add_test(NAME pipeline_preload COMMAND rb_blitz_pipeline_preload_tests)

    if(WIN32 AND REXGLUE_USE_VULKAN)
        add_executable(rb_blitz_vulkan_texture_descriptor_cache_tests
            tests/vulkan_texture_descriptor_cache_tests.cpp)
        target_include_directories(rb_blitz_vulkan_texture_descriptor_cache_tests PRIVATE
            "${REXSDK_DIR}/include")
        target_link_libraries(rb_blitz_vulkan_texture_descriptor_cache_tests PRIVATE Vulkan::Headers)
        add_test(NAME vulkan_texture_descriptor_cache COMMAND rb_blitz_vulkan_texture_descriptor_cache_tests)

        add_executable(rb_blitz_vulkan_texture_conversion_tests
            tests/vulkan_texture_conversion_tests.cpp
            "${REXSDK_DIR}/thirdparty/glslang/StandAlone/ResourceLimits.cpp")
        target_include_directories(rb_blitz_vulkan_texture_conversion_tests PRIVATE
            "${REXSDK_DIR}/include" "${REXSDK_DIR}/thirdparty/glslang")
        target_link_libraries(rb_blitz_vulkan_texture_conversion_tests PRIVATE
            Vulkan::Headers glslang::SPIRV glslang)
        add_test(NAME vulkan_texture_conversion COMMAND rb_blitz_vulkan_texture_conversion_tests)
        set_tests_properties(vulkan_texture_conversion PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if(WIN32)
        add_test(NAME sdk_patch_stack
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdk_patch_stack_tests.py")
    endif()
    if(WIN32 AND REXGLUE_USE_D3D12)
        add_executable(rb_blitz_d3d12_fence_sync_tests tests/d3d12_fence_sync_tests.cpp)
        target_include_directories(rb_blitz_d3d12_fence_sync_tests PRIVATE "${REXSDK_DIR}/include")
        target_link_libraries(rb_blitz_d3d12_fence_sync_tests PRIVATE d3d12 dxgi)
        add_test(NAME d3d12_fence_sync COMMAND rb_blitz_d3d12_fence_sync_tests)
        set_tests_properties(d3d12_fence_sync PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    # Exercise the component conversion shared by the patched D3D12 and Vulkan texture caches.
    add_executable(rb_blitz_texture_conversion_tests tests/texture_conversion_tests.cpp)
    target_link_libraries(rb_blitz_texture_conversion_tests PRIVATE rex::core)
    if(WIN32)
        target_link_libraries(rb_blitz_texture_conversion_tests PRIVATE ws2_32)
    endif()
    add_test(NAME texture_conversion COMMAND rb_blitz_texture_conversion_tests)

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

    # The HUD re-anchor rule (src/hooks/hud_layout.h). Header-only: the policy is arithmetic
    # and has no boot frame, no guest and no SDK in it, which is the point of the seam.
    add_executable(rb_blitz_hud_layout_tests
        tests/hud_layout_tests.cpp
    )
    target_include_directories(rb_blitz_hud_layout_tests PRIVATE src)
    add_test(NAME hud_layout COMMAND rb_blitz_hud_layout_tests)

    # The window's own size (src/settings/window_policy.h, P2): how a fixed-size window is fitted
    # into the display's usable area without changing its aspect, and the physical -> logical
    # conversion `window_width`/`window_height` are expressed in. SDK-free arithmetic like the HUD
    # rule above, and tested for the same reason - the rule is the requirement.
    add_executable(rb_blitz_window_policy_tests
        tests/window_policy_tests.cpp
        src/settings/window_policy.cpp
    )
    target_include_directories(rb_blitz_window_policy_tests PRIVATE src)
    add_test(NAME window_policy COMMAND rb_blitz_window_policy_tests)

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

    add_executable(rb_blitz_native_mouse_tests
        tests/native_mouse_tests.cpp
    )
    target_include_directories(rb_blitz_native_mouse_tests PRIVATE src)
    add_test(NAME native_mouse COMMAND rb_blitz_native_mouse_tests)

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

    add_executable(rb_blitz_content_progress_tests tests/content_progress_tests.cpp)
    target_include_directories(rb_blitz_content_progress_tests PRIVATE src)
    add_test(NAME content_progress COMMAND rb_blitz_content_progress_tests)

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
        # P9-1 (RhythmVerse plan §9): the suite pins the store's master switch to the schema's own
        # compiled default, so the schema table is linked rather than re-spelled here.
        src/settings/model.cpp
        src/settings/schema.cpp
        # P7: the surface now emits the Controls rows, whose token grammar and labels live in the
        # profile's own vocabulary.
        src/profile/input_rows.cpp
        src/profile/remap.cpp
        src/profile/gameplay_remap.cpp
        # The help captions' retail-page resolver: its table is half of the surface's caption
        # coverage (the other half lives in settings_screen.cpp), so it is tested beside it.
        src/ui/help_topics.cpp
    )
    target_include_directories(rb_blitz_settings_screen_tests PRIVATE src)
    add_test(NAME settings_screen COMMAND rb_blitz_settings_screen_tests)

    # The archive half of the generated Help & Options payload (src/settings/menu_payload.cpp):
    # that extending the mounted patch archive keeps it a *one-part* archive, because the title has
    # never read a two-part one and a boot that offered it one hung inside our own read hook
    # (docs/settings/status.md (part 2, the black startup)). The assertions run through
    # src/settings/ultimate_adapter.cpp's own v6 reader, so the fixture and the product cannot drift
    # apart. SDK-free: the fixtures are an index and bytes, no retail content.
    add_executable(rb_blitz_menu_payload_tests
        tests/menu_payload_tests.cpp
        src/settings/menu_payload.cpp
        src/settings/ultimate_adapter.cpp
        src/ui/settings_screen.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
        src/profile/input_rows.cpp
        src/profile/remap.cpp
        src/profile/gameplay_remap.cpp
        # The retail store screen's payload edits (`FindNetcacheInit`, `PointNetcacheAtLocalServer`,
        # `PointUiNetcacheAtLocalServer`, `AddStoreEntryBypass`) live here; without it the target
        # links only while nothing in menu_payload.cpp reaches that section.
        src/store/store_payload.cpp
    )
    target_include_directories(rb_blitz_menu_payload_tests PRIVATE src)
    add_test(NAME menu_payload COMMAND rb_blitz_menu_payload_tests)

    # R9's pool and deck (src/hooks/menu_music_pool.h): that a merged pack's songs are all
    # pooled and sampled at random, and that no song is ever dealt twice in a row. The hook
    # that uses it (src/hooks/shell_music.cpp) needs the SDK and a boot; this half does not.
    add_executable(rb_blitz_menu_music_pool_tests
        tests/menu_music_pool_tests.cpp
        src/hooks/menu_music_metadata.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
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

    # The controller profile: path resolution and the reader/writer the game's own input
    # configuration uses (docs/settings/contracts.md §2). SDK-free, so it runs without booting
    # anything. The suite was P0.3's `controller_settings_profile` until the external settings
    # executable retired (gameplay-mapping plan §19); the checks are unchanged.
    add_executable(rb_blitz_profile_tests
        tests/profile_tests.cpp
        src/profile/profile.cpp
        src/profile/profile_path.cpp
    )
    target_include_directories(rb_blitz_profile_tests PRIVATE src)
    add_test(NAME profile COMMAND rb_blitz_profile_tests)

    # D16's vocabulary: the `[remap]` grammar, what the profile round-trips, and the rewrite
    # itself. Dependency-free like the profile tests - no pad, no window, no SDL - because the
    # device side is only the callback the game hands the rewrite (src/input/remap.cpp). Renamed
    # from `controller_settings_remap` when the external executable retired; same checks.
    add_executable(rb_blitz_profile_remap_tests
        tests/profile_remap_tests.cpp
        src/profile/profile.cpp
        src/profile/remap.cpp
    )
    target_include_directories(rb_blitz_profile_remap_tests PRIVATE src)
    add_test(NAME profile_remap COMMAND rb_blitz_profile_remap_tests)

    # P1 of the gameplay-mapping plan: the two gameplay tables, their per-table source kinds,
    # their compiled defaults and the profile storage all three tables share. Dependency-free like
    # the remap suite: no pad, no window, no SDL, no boot.
    add_executable(rb_blitz_gameplay_remap_tests
        tests/gameplay_remap_tests.cpp
        src/profile/profile.cpp
        src/profile/gameplay_remap.cpp
        src/profile/remap.cpp
    )
    target_include_directories(rb_blitz_gameplay_remap_tests PRIVATE src)
    add_test(NAME gameplay_remap COMMAND rb_blitz_gameplay_remap_tests)

    # P2 of the gameplay-mapping plan: the source gates, the physical snapshot's source-kind
    # classification, the per-table exclusion of the other kind, the action edges and the
    # boundary rule. SDK-free and SDL-free - the device layer is the plain data a sampler hands
    # in - so the rules are pinned without a pad, a window or a boot.
    add_executable(rb_blitz_input_gates_tests
        tests/input_gates_tests.cpp
        src/input/source_gates.cpp
        src/profile/profile.cpp
        src/profile/gameplay_remap.cpp
        src/profile/remap.cpp
    )
    target_include_directories(rb_blitz_input_gates_tests PRIVATE src)
    add_test(NAME input_gates COMMAND rb_blitz_input_gates_tests)

    # P7 of the gameplay-mapping plan: the in-game Controls rows. The `input.*` grammar, the
    # listening session's release-first and wrong-kind rules, and the store's one-writer save
    # against a temporary profile. SDK-free and SDL-free, like the store itself.
    add_executable(rb_blitz_input_bindings_tests
        tests/input_bindings_tests.cpp
        src/input/input_capture.cpp
        src/input/input_store.cpp
        src/input/effective_input.cpp
        src/profile/input_rows.cpp
        src/profile/profile.cpp
        src/profile/profile_path.cpp
        src/profile/gameplay_remap.cpp
        src/profile/remap.cpp
    )
    target_include_directories(rb_blitz_input_bindings_tests PRIVATE src)
    add_test(NAME input_bindings COMMAND rb_blitz_input_bindings_tests)

    # P3 (docs/settings/contracts.md §5): the SDK-free settings store. The
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
    # the same files, and from src/profile because the migration consumes the profile model.
    add_executable(rb_blitz_settings_migration_tests
        tests/settings_migration_tests.cpp
        src/settings/model.cpp
        src/settings/schema.cpp
        src/settings/store.cpp
        src/settings/paths.cpp
        src/settings/migration.cpp
        src/profile/profile.cpp
        src/profile/profile_path.cpp
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
        src/profile/profile.cpp
        src/profile/profile_path.cpp
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
        src/profile/profile.cpp
        src/profile/profile_path.cpp
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
        src/settings/capabilities.cpp
        src/settings/graphics_policy.cpp
        src/settings/model.cpp
        src/settings/schema.cpp
        src/settings/store.cpp
        src/settings/paths.cpp
        src/settings/migration.cpp
        src/settings/cvars.cpp
        src/settings/service.cpp
        src/settings/bridge_sink.cpp
        src/hooks/settings_bridge.cpp
        src/profile/profile.cpp
        src/profile/profile_path.cpp
        src/profile/input_rows.cpp
        src/profile/remap.cpp
        src/profile/gameplay_remap.cpp
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

    # P0 of the RhythmVerse store plan (src/store/rhythmverse_client.h): the request body, the
    # response model over the committed captures, the direct-link/archive/length policy and the
    # artist/title matcher. SDK-free and hermetic; the fixtures are tracked under
    # docs/rythmverse-responses/, so the suite needs the project dir rather than a game install.
    add_executable(rb_blitz_store_client_tests
        tests/store_client_tests.cpp
        src/store/rhythmverse_client.cpp
        src/store/store_settings.cpp
    )
    target_include_directories(rb_blitz_store_client_tests PRIVATE
        src
        "${REXSDK_DIR}/thirdparty/inja/third_party/include")
    if(WIN32)
        # The manual `--live-list` probe is the only ctest-adjacent code that touches the network;
        # the suite itself never sends a request.
        target_sources(rb_blitz_store_client_tests PRIVATE src/store/store_http.cpp)
        target_link_libraries(rb_blitz_store_client_tests PRIVATE winhttp)
    endif()
    add_test(NAME store_client
             COMMAND rb_blitz_store_client_tests --project-dir "${CMAKE_CURRENT_SOURCE_DIR}")

    # P2 of the RhythmVerse store plan (src/store/store_payload.h): the `netcache_init` rewrite that
    # points the retail store at a local (host-file) server. SDK-free; the round-trip case compiles
    # and parses the record with the project's own DTA writer, so it links that module too.
    add_executable(rb_blitz_store_payload_tests
        tests/store_payload_tests.cpp
        src/store/store_payload.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
    )
    target_include_directories(rb_blitz_store_payload_tests PRIVATE src)
    add_test(NAME store_payload COMMAND rb_blitz_store_payload_tests)

    # P2's exit needs the writer skeleton (src/store/store_metadata.h): the request names and the
    # front-menu record. The schema is still a guess, so the suite pins the names, the refusals and
    # the compile/parse round trip - never the retail parser, which no boot has reached yet.
    add_executable(rb_blitz_store_metadata_tests
        tests/store_metadata_tests.cpp
        src/store/store_metadata.cpp
        src/store/album_art.cpp
        src/store/rhythmverse_client.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
    )
    target_include_directories(rb_blitz_store_metadata_tests PRIVATE src
        "${REXSDK_DIR}/thirdparty/inja/third_party/include")
    add_test(NAME store_metadata COMMAND rb_blitz_store_metadata_tests)

    # P3's data lane: the text record writers and the serving policy (src/store/store_serve.h).
    # The netcache serves `.dta` as TEXT, so the records a boot is answered with are text - the
    # suite pins the shapes, the refusals, the path rules and the file-over-builtin choice.
    add_executable(rb_blitz_store_serve_tests
        tests/store_serve_tests.cpp
        src/store/store_serve.cpp
        src/store/store_metadata.cpp
        src/store/album_art.cpp
        src/store/rhythmverse_client.cpp
        src/ui/dtb_author.cpp
        src/ui/menu_options.cpp
    )
    target_include_directories(rb_blitz_store_serve_tests PRIVATE src
        "${REXSDK_DIR}/thirdparty/inja/third_party/include")
    add_test(NAME store_serve COMMAND rb_blitz_store_serve_tests)

    # P3's album art (src/assets/texture_codec.h + src/store/album_art.h): the platform texture
    # container, the DXT1/DXT5 block codec, the resampler, the key/path rule, the URL policy and the
    # texture cache. SDK-free and hermetic - the worker that fetches and decodes a cover needs
    # Windows and a network, and everything it decides is pinned here.
    add_executable(rb_blitz_store_art_tests
        tests/store_art_tests.cpp
        src/assets/texture_codec.cpp
        src/store/album_art.cpp
    )
    target_include_directories(rb_blitz_store_art_tests PRIVATE src)
    add_test(NAME store_art COMMAND rb_blitz_store_art_tests)

    # P3's Downloaded matcher (src/store/store_library.h): the field normalization and the
    # (artist, title) index. Hermetic and SDK-free - the package reader is the builder's half and
    # needs the SDK, while everything the mark decides is in this tuple comparison.
    add_executable(rb_blitz_store_library_tests
        tests/store_library_tests.cpp
        src/store/store_library_index.cpp
    )
    target_include_directories(rb_blitz_store_library_tests PRIVATE src)
    add_test(NAME store_library COMMAND rb_blitz_store_library_tests)

    # P5 of the RhythmVerse store plan: the download pipeline. SDK-free and hermetic - the
    # transfer is a fake and the destination is a temporary folder - so the redirect/archive/
    # length/identity policy, the staging and commit rules, the bridge grammar and the job
    # service's queue/cancel/receipt/refresh behaviour all run without a boot or a network.
    add_executable(rb_blitz_store_download_tests
        tests/store_download_tests.cpp
        src/store/store_download.cpp
        src/store/store_install.cpp
        src/store/store_download_service.cpp
        src/store/store_settings.cpp
        src/store/rhythmverse_client.cpp
        src/util/sha256.cpp
    )
    target_include_directories(rb_blitz_store_download_tests PRIVATE
        src
        "${REXSDK_DIR}/thirdparty/inja/third_party/include")
    add_test(NAME store_download COMMAND rb_blitz_store_download_tests)

    # P4 of the RhythmVerse store plan (src/store/store_search.h): the typed field (UTF-8, bounded,
    # one code point per backspace), the key-token transport, the request it becomes, the answer it
    # accepts and the one-bit replies. SDK-free and hermetic: the transport is a submit sink the
    # test counts, so a case can "perform" a request without a socket. The authored key pages are
    # pinned by `settings_screen`, which links the whole surface.
    add_executable(rb_blitz_store_search_tests
        tests/store_search_tests.cpp
        src/store/store_search.cpp
        src/store/store_settings.cpp
        src/store/rhythmverse_client.cpp
    )
    target_include_directories(rb_blitz_store_search_tests PRIVATE
        src
        "${REXSDK_DIR}/thirdparty/inja/third_party/include")
    if(WIN32)
        # The manual `--live-search` arm is the only ctest-adjacent code that touches the network;
        # the suite itself never sends a request (store_client's `--live-list` has the same rule).
        target_sources(rb_blitz_store_search_tests PRIVATE src/store/store_http.cpp)
        target_link_libraries(rb_blitz_store_search_tests PRIVATE winhttp)
    endif()
    add_test(NAME store_search COMMAND rb_blitz_store_search_tests)

    # P7 of the RhythmVerse store plan: the in-memory page cache both store workers share. SDK-free
    # and hermetic - the TTL, the bound, the eviction order and single-flight run on plain data, and
    # the clock is the real one with short windows.
    add_executable(rb_blitz_store_page_cache_tests
        tests/store_page_cache_tests.cpp
        src/store/store_page_cache.cpp
    )
    target_include_directories(rb_blitz_store_page_cache_tests PRIVATE src)
    add_test(NAME store_page_cache COMMAND rb_blitz_store_page_cache_tests)

    # P7's store sentences (src/store/store_catalogue_status.h): the loading line, the empty state
    # and the offline text, with the precedence between them and the window that keeps them inside
    # the store. The credit line they used to include was removed on the owner's request
    # (2026-10-23). SDK-free; the suite shrinks the window to prove expiry.
    add_executable(rb_blitz_store_catalogue_status_tests
        tests/store_catalogue_status_tests.cpp
        src/store/store_catalogue_status.cpp
    )
    target_include_directories(rb_blitz_store_catalogue_status_tests PRIVATE src)
    add_test(NAME store_catalogue_status COMMAND rb_blitz_store_catalogue_status_tests)
endif()

# Controller Settings used to be a second executable in this build tree (launcher plan D1). It
# retired on 2026-10-19: every player setting it edited is an in-game Help & Options row, so the
# build produces rb_blitz.exe only (gameplay-mapping plan §19).
