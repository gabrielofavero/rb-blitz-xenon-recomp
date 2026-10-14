# Controller Settings (docs/plans/finished/2026-09-27-launcher-plan.md). This directory holds Controller Settings' own
# sources, tools and configuration, and the rb_blitz_controller_settings executable itself.
#
# The settings schema is the first thing here the build needs. controller-settings/config/
# settings.toml is the single source of the rows Controller Settings shows, and
# tools/embed_settings.cpp compiles - and validates - it into a generated header, so a
# row cannot exist in the UI without a tooltip, and cannot be saved without being
# launchable (launcher-plan.md section 4.1). The header lands in the source tree next to
# the installer's generated files because Controller Settings' own sources include it by path;
# /controller-settings/out/ is ignored.

add_executable(rb_blitz_embed_settings tools/embed_settings.cpp)
target_compile_features(rb_blitz_embed_settings PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(rb_blitz_embed_settings PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(rb_blitz_embed_settings PRIVATE -Wall -Wextra)
endif()

set(RBBLITZ_SETTINGS_TOML "${CMAKE_CURRENT_SOURCE_DIR}/config/settings.toml")
set(RBBLITZ_SETTINGS_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/out/generated/settings_table.h")

# Which graphics backends the payload was built with decides which renderer Controller Settings may
# offer: the GPU plugin cannot run a backend it was not compiled with, so the schema's
# `choices_from = "gpu_backends"` row gets exactly this list - not a list written by hand that
# could drift from the build. A payload with one backend therefore offers one renderer.
set(RBBLITZ_BACKENDS "")
if(REXGLUE_USE_D3D12)
    list(APPEND RBBLITZ_BACKENDS d3d12)
endif()
if(REXGLUE_USE_VULKAN)
    list(APPEND RBBLITZ_BACKENDS vulkan)
endif()
string(REPLACE ";" "," RBBLITZ_BACKENDS_ARG "${RBBLITZ_BACKENDS}")

set(RBBLITZ_SETTINGS_ARGS)
if(RBBLITZ_BACKENDS)
    list(APPEND RBBLITZ_SETTINGS_ARGS "--backends=${RBBLITZ_BACKENDS_ARG}")
endif()

add_custom_command(
    OUTPUT "${RBBLITZ_SETTINGS_HEADER}"
    COMMAND $<TARGET_FILE:rb_blitz_embed_settings>
            --settings "${RBBLITZ_SETTINGS_TOML}"
            --header "${RBBLITZ_SETTINGS_HEADER}"
            ${RBBLITZ_SETTINGS_ARGS}
    DEPENDS rb_blitz_embed_settings "${RBBLITZ_SETTINGS_TOML}"
    COMMENT "Embedding Controller Settings settings schema"
    VERBATIM)

# A target of its own so the schema can be regenerated - and revalidated - without
# building Controller Settings: cmake --build <dir> --target rb_blitz_controller_settings_schema.
add_custom_target(rb_blitz_controller_settings_schema DEPENDS "${RBBLITZ_SETTINGS_HEADER}")

# Controller Settings executable (P0.4). It links the SDK's already-configured imgui OBJECT
# target and the static SDL3 the game links, and compiles its own ImGui platform and
# renderer backends - the renderer is SDL_GPU, chosen in D1 and explained in README.md.
# It deliberately does not link rex::runtime: a settings window has no use for the
# emulator, and D10's import list is what keeps that honest.
set(RBBLITZ_IMGUI_BACKENDS
    "${REXSDK_DIR}/thirdparty/imgui/backends/imgui_impl_sdl3.cpp"
    "${REXSDK_DIR}/thirdparty/imgui/backends/imgui_impl_sdlgpu3.cpp")

# Vendored backend sources are compiled, not owned: silence their warnings the same way
# the imgui OBJECT target does.
set_source_files_properties(${RBBLITZ_IMGUI_BACKENDS} PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:Clang,GNU>:-w>")

set(RBBLITZ_CONTROLLER_SETTINGS_SOURCES
    main.cpp
    src/controller_tab.cpp
    src/focus_log.cpp
    src/game_config.cpp
    src/game_launch.cpp
    src/general_report.cpp
    src/general_tab.cpp
    src/install_ultimate.cpp
    src/nav.cpp
    src/nav_bindings.cpp
    src/nav_keys.cpp
    src/pad_nav.cpp
    src/pad_source.cpp
    src/path_validate.cpp
    src/prefill.cpp
    src/profile_session.cpp
    src/profile_ui.cpp
    src/row_ui.cpp
    src/schema_view.cpp
    src/settings_edit.cpp
    src/shell.cpp
    src/ultimate_state.cpp
    src/update_check.cpp
    src/updater.cpp
    src/virtual_pad.cpp
    # The profile and remap modules are shared with the game and dependency-free on purpose
    # (P0.3, D16), which is what lets Controller Settings compile them too: the profile module is how
    # A1 remembers the window geometry, and the remap module is the vocabulary the Controller
    # tab writes and the game reads back.
    "${CMAKE_SOURCE_DIR}/src/profile/profile.cpp"
    "${CMAKE_SOURCE_DIR}/src/profile/profile_path.cpp"
    "${CMAKE_SOURCE_DIR}/src/profile/remap.cpp"
    ${RBBLITZ_IMGUI_BACKENDS})

if(WIN32)
    # Same icon mechanism as the game (root CMakeLists.txt): the .rc carries
    # assets/icon/blitz-mono.ico, the black-and-white alt icon Controller Settings ships so its
    # window is told apart from the game's, and the icon is bound to the resource object
    # by hand because the resource compiler does not report the files it inlines.
    set_source_files_properties(rb_blitz_controller_settings.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/icon/blitz-mono.ico")
    add_executable(rb_blitz_controller_settings WIN32 ${RBBLITZ_CONTROLLER_SETTINGS_SOURCES}
        rb_blitz_controller_settings.rc)
else()
    add_executable(rb_blitz_controller_settings ${RBBLITZ_CONTROLLER_SETTINGS_SOURCES})
endif()

# The release version the window title shows. It is read out of installer/config/pins.toml - the
# project's own release record, and the file the setup executable is named from - rather than
# written again here, so Controller Settings and the installer that shipped it cannot claim to be different
# releases. A release that stamps its version some other way (a tag, CI) passes it instead:
# -DRBBLITZ_VERSION=1.2.3.
#
# The update channel is read from the same file, for the same reason (D19): Controller Settings that
# checked a URL the release it shipped with did not publish would either find nothing or
# find someone else's file, and neither is visible until a user complains.
file(READ "${CMAKE_SOURCE_DIR}/installer/config/pins.toml" RBBLITZ_PINS_TOML)
# Comment lines are dropped before the sections are cut out. A section is read as "from [x] up to
# the next '['", so a bracket anywhere in between ends it - including one in a comment, which is
# how `(`[name]`` on the [installer] section's first comment line hid `version = "0.1.0"` from the
# reader below and failed *every* fresh configure of this tree with "no [installer] version in
# installer/config/pins.toml" (measured 2026-10-14; the installer and Controller Settings' own
# CMake projects do not read the file this way, which is why it went unnoticed).
string(REPLACE "\n" ";" RBBLITZ_PINS_LINES "${RBBLITZ_PINS_TOML}")
set(RBBLITZ_PINS_ACTIVE "")
foreach(rbblitz_pins_line IN LISTS RBBLITZ_PINS_LINES)
    string(STRIP "${rbblitz_pins_line}" rbblitz_pins_stripped)
    if(rbblitz_pins_stripped MATCHES "^#")
        continue()
    endif()
    string(APPEND RBBLITZ_PINS_ACTIVE "${rbblitz_pins_line}\n")
endforeach()
string(REGEX MATCH "\\[installer\\][^\\[]*" RBBLITZ_INSTALLER_PINS "${RBBLITZ_PINS_ACTIVE}")
string(REGEX MATCH "\\[update\\][^\\[]*" RBBLITZ_UPDATE_PINS "${RBBLITZ_PINS_ACTIVE}")

set(RBBLITZ_VERSION "" CACHE STRING "Version shown in Controller Settings' window title")
if(RBBLITZ_VERSION STREQUAL "")
    string(REGEX MATCH "version[ \t]*=[ \t]*\"([0-9][^\"]*)\"" RBBLITZ_PINS_VERSION
                 "${RBBLITZ_INSTALLER_PINS}")
    set(RBBLITZ_VERSION "${CMAKE_MATCH_1}")
    if(RBBLITZ_VERSION STREQUAL "")
        message(FATAL_ERROR
            "Controller Settings: no [installer] version in installer/config/pins.toml. Controller Settings' window "
            "title is the release version, so a release that cannot read it is worse than one that "
            "does not build: fix that file, or configure with -DRBBLITZ_VERSION=<version>.")
    endif()
endif()

# Where Controller Settings asks whether a newer release exists, and where it sends a user whose
# update cannot be installed automatically. An empty manifest_url is a supported
# configuration - a build with no release channel - and Controller Settings then checks nothing and
# says nothing, which is what D19 asks of a check that cannot be completed.
#
# Read from pins.toml, or passed in: -DRBBLITZ_UPDATE_URL=<url> is what a build that has to
# check a channel other than the release one uses (a staging release, or a test server serving
# an update.toml - which is how --dump-update is exercised on a build machine without a
# release existing yet). An empty value means "read pins.toml".
set(RBBLITZ_UPDATE_URL "" CACHE STRING "Release manifest Controller Settings' update check fetches")
if(RBBLITZ_UPDATE_URL STREQUAL "")
    # Searched over the whole file rather than inside the [update] table: the key is written
    # once, and a section-scoped match would end at the first bracket in a comment that
    # explains the pins - which is how a documented value stops being read.
    string(REGEX MATCH "manifest_url[ \t]*=[ \t]*\"([^\"]*)\"" RBBLITZ_UPDATE_MATCH
                 "${RBBLITZ_PINS_TOML}")
    set(RBBLITZ_UPDATE_URL "${CMAKE_MATCH_1}")
endif()
string(REGEX MATCH "homepage_url[ \t]*=[ \t]*\"([^\"]*)\"" RBBLITZ_HOMEPAGE_MATCH
             "${RBBLITZ_INSTALLER_PINS}")
set(RBBLITZ_HOMEPAGE_URL "${CMAKE_MATCH_1}")

target_compile_features(rb_blitz_controller_settings PRIVATE cxx_std_20)
# Quotes included: the macro is a C++ string literal in Controller Settings' sources, so the value has to
# arrive as one command-line argument with the quotes still around it.
target_compile_definitions(rb_blitz_controller_settings PRIVATE
    RBBLITZ_CONTROLLER_SETTINGS_VERSION="${RBBLITZ_VERSION}"
    RBBLITZ_CONTROLLER_SETTINGS_UPDATE_URL="${RBBLITZ_UPDATE_URL}"
    RBBLITZ_CONTROLLER_SETTINGS_RELEASES_URL="${RBBLITZ_HOMEPAGE_URL}/releases/latest")

# D1 puts Controller Settings next to rb_blitz.exe in the build root rather than beside this
# file, because that is the directory the payload snapshot reads (P0.5) and the one a
# user is told to run the game from. Asking for rb_blitz's own output directory keeps
# the two together under any generator or CMAKE_RUNTIME_OUTPUT_DIRECTORY.
set_target_properties(rb_blitz_controller_settings PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:rb_blitz>")

if(MSVC)
    target_compile_options(rb_blitz_controller_settings PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(rb_blitz_controller_settings PRIVATE -Wall -Wextra)
endif()

# imgui's target exposes only its own directory; the backends live beside it, and the
# Controller Settings includes them directly.
target_include_directories(rb_blitz_controller_settings PRIVATE
    "${REXSDK_DIR}/thirdparty/imgui/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"            # nav.h, schema_view.h, shell.h
    "${CMAKE_CURRENT_SOURCE_DIR}/out/generated"  # settings_table.h (P0.2)
    "${CMAKE_SOURCE_DIR}/src")                   # src/profile/profile*.h

# The shell includes the generated table, so the table has to exist before it compiles.
add_dependencies(rb_blitz_controller_settings rb_blitz_controller_settings_schema)

target_link_libraries(rb_blitz_controller_settings PRIVATE imgui SDL3::SDL3)

if(WIN32)
    # B8 opens the OS shell (the default browser for the pinned release URL, Explorer for the
    # folder a manual install goes into) through ShellExecuteW. D19's update check is one HTTPS
    # GET, over WinHTTP, for the same reason the installer's helper uses it: it is part of
    # Windows, it honours the machine's proxy configuration, and it needs no extra DLL.
    target_link_libraries(rb_blitz_controller_settings PRIVATE shell32 winhttp)
endif()

# B7's launch contract, as a test: the argv Controller Settings hands the game is built from the
# schema, so this target needs the generated table - which is why it lives here rather than
# beside the other Controller Settings tests in the root CMakeLists.txt. No ImGui, no SDL, no process:
# BuildLaunchCommand is pure string work.
if(BUILD_TESTING)
    add_executable(rb_blitz_controller_settings_launch_tests
        "${CMAKE_SOURCE_DIR}/tests/controller_settings_launch_tests.cpp"
        src/game_launch.cpp
        src/game_config.cpp
        src/profile_session.cpp
        "${CMAKE_SOURCE_DIR}/src/profile/profile.cpp"
        "${CMAKE_SOURCE_DIR}/src/profile/profile_path.cpp"
    )
    target_include_directories(rb_blitz_controller_settings_launch_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/out/generated")
    add_dependencies(rb_blitz_controller_settings_launch_tests rb_blitz_controller_settings_schema)
    # Next to Controller Settings and the game, like every other test executable this tree builds.
    set_target_properties(rb_blitz_controller_settings_launch_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:rb_blitz>")
    if(MSVC)
        target_compile_options(rb_blitz_controller_settings_launch_tests PRIVATE /W4 /utf-8 /EHsc)
    else()
        target_compile_options(rb_blitz_controller_settings_launch_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME controller_settings_launch COMMAND rb_blitz_controller_settings_launch_tests)

    # A3's pad rules, A2's help, and A5's keys: the deadzone, the repeat, the binding table, the
    # binding table A5 makes rebindable - with its chord precedence and its `[nav]` round trip -
    # and the walk from a ring entry to the row the bar names. All dependency-free - no SDL, no
    # ImGui, no window - which is the whole reason the pad's decisions live in pad_nav.cpp and the
    # keys' in nav_bindings.cpp rather than in the glue beside them. It needs the generated table
    # for the same reason the launch tests do.
    add_executable(rb_blitz_controller_settings_nav_tests
        "${CMAKE_SOURCE_DIR}/tests/controller_settings_pad_nav_tests.cpp"
        "${CMAKE_SOURCE_DIR}/tests/controller_settings_help_tests.cpp"
        "${CMAKE_SOURCE_DIR}/tests/controller_settings_keys_tests.cpp"
        src/pad_nav.cpp
        src/nav_bindings.cpp
        src/schema_view.cpp
        "${CMAKE_SOURCE_DIR}/src/profile/profile.cpp"
    )
    target_include_directories(rb_blitz_controller_settings_nav_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/out/generated")
    add_dependencies(rb_blitz_controller_settings_nav_tests rb_blitz_controller_settings_schema)
    set_target_properties(rb_blitz_controller_settings_nav_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:rb_blitz>")
    if(MSVC)
        target_compile_options(rb_blitz_controller_settings_nav_tests PRIVATE /W4 /utf-8 /EHsc)
    else()
        target_compile_options(rb_blitz_controller_settings_nav_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME controller_settings_nav COMMAND rb_blitz_controller_settings_nav_tests)
endif()
