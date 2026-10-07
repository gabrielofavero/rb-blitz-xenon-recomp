# The launcher (docs/plans/launcher-plan.md). This directory holds the launcher's own
# sources, tools and configuration, and the rb_blitz_launcher executable itself.
#
# The settings schema is the first thing here the build needs. launcher/config/
# settings.toml is the single source of the rows the launcher shows, and
# tools/embed_settings.cpp compiles - and validates - it into a generated header, so a
# row cannot exist in the UI without a tooltip, and cannot be saved without being
# launchable (launcher-plan.md section 4.1). The header lands in the source tree next to
# the installer's generated files because the launcher's own sources include it by path;
# /launcher/out/ is ignored.

add_executable(rb_blitz_embed_settings tools/embed_settings.cpp)
target_compile_features(rb_blitz_embed_settings PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(rb_blitz_embed_settings PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(rb_blitz_embed_settings PRIVATE -Wall -Wextra)
endif()

set(RBBLITZ_SETTINGS_TOML "${CMAKE_CURRENT_SOURCE_DIR}/config/settings.toml")
set(RBBLITZ_SETTINGS_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/out/generated/settings_table.h")

# Which graphics backends the payload was built with decides which renderer the launcher may
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
    COMMENT "Embedding the launcher settings schema"
    VERBATIM)

# A target of its own so the schema can be regenerated - and revalidated - without
# building the launcher: cmake --build <dir> --target rb_blitz_launcher_settings.
add_custom_target(rb_blitz_launcher_settings DEPENDS "${RBBLITZ_SETTINGS_HEADER}")

# The launcher executable (P0.4). It links the SDK's already-configured imgui OBJECT
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

set(RBBLITZ_LAUNCHER_SOURCES
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
    src/update_launcher.cpp
    src/virtual_pad.cpp
    # The profile and remap modules are shared with the game and dependency-free on purpose
    # (P0.3, D16), which is what lets the launcher compile them too: the profile module is how
    # A1 remembers the window geometry, and the remap module is the vocabulary the Controller
    # tab writes and the game reads back.
    "${CMAKE_SOURCE_DIR}/src/launcher/profile.cpp"
    "${CMAKE_SOURCE_DIR}/src/launcher/profile_path.cpp"
    "${CMAKE_SOURCE_DIR}/src/launcher/remap.cpp"
    ${RBBLITZ_IMGUI_BACKENDS})

if(WIN32)
    # Same icon mechanism as the game (root CMakeLists.txt): the .rc carries
    # assets/blitz.ico, and the icon is bound to the resource object by hand because the
    # resource compiler does not report the files it inlines.
    set_source_files_properties(rb_blitz_launcher.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/blitz.ico")
    add_executable(rb_blitz_launcher WIN32 ${RBBLITZ_LAUNCHER_SOURCES}
        rb_blitz_launcher.rc)
else()
    add_executable(rb_blitz_launcher ${RBBLITZ_LAUNCHER_SOURCES})
endif()

# The release version the window title shows. It is read out of installer/config/pins.toml - the
# project's own release record, and the file the setup executable is named from - rather than
# written again here, so a launcher and the installer that shipped it cannot claim to be different
# releases. A release that stamps its version some other way (a tag, CI) passes it instead:
# -DRBBLITZ_VERSION=1.2.3.
#
# The update channel is read from the same file, for the same reason (D19): a launcher that
# checked a URL the release it shipped with did not publish would either find nothing or
# find someone else's file, and neither is visible until a user complains.
file(READ "${CMAKE_SOURCE_DIR}/installer/config/pins.toml" RBBLITZ_PINS_TOML)
string(REGEX MATCH "\\[installer\\][^\\[]*" RBBLITZ_INSTALLER_PINS "${RBBLITZ_PINS_TOML}")
string(REGEX MATCH "\\[update\\][^\\[]*" RBBLITZ_UPDATE_PINS "${RBBLITZ_PINS_TOML}")

set(RBBLITZ_VERSION "" CACHE STRING "Version shown in the launcher's window title")
if(RBBLITZ_VERSION STREQUAL "")
    string(REGEX MATCH "version[ \t]*=[ \t]*\"([0-9][^\"]*)\"" RBBLITZ_PINS_VERSION
                 "${RBBLITZ_INSTALLER_PINS}")
    set(RBBLITZ_VERSION "${CMAKE_MATCH_1}")
    if(RBBLITZ_VERSION STREQUAL "")
        message(FATAL_ERROR
            "launcher: no [installer] version in installer/config/pins.toml. The launcher's window "
            "title is the release version, so a release that cannot read it is worse than one that "
            "does not build: fix that file, or configure with -DRBBLITZ_VERSION=<version>.")
    endif()
endif()

# Where the launcher asks whether a newer release exists, and where it sends a user whose
# update cannot be installed automatically. An empty manifest_url is a supported
# configuration - a build with no release channel - and the launcher then checks nothing and
# says nothing, which is what D19 asks of a check that cannot be completed.
#
# Read from pins.toml, or passed in: -DRBBLITZ_UPDATE_URL=<url> is what a build that has to
# check a channel other than the release one uses (a staging release, or a test server serving
# an update.toml - which is how --dump-update is exercised on a build machine without a
# release existing yet). An empty value means "read pins.toml".
set(RBBLITZ_UPDATE_URL "" CACHE STRING "Release manifest the launcher's update check fetches")
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

target_compile_features(rb_blitz_launcher PRIVATE cxx_std_20)
# Quotes included: the macro is a C++ string literal in the launcher's sources, so the value has to
# arrive as one command-line argument with the quotes still around it.
target_compile_definitions(rb_blitz_launcher PRIVATE
    RBBLITZ_LAUNCHER_VERSION="${RBBLITZ_VERSION}"
    RBBLITZ_LAUNCHER_UPDATE_URL="${RBBLITZ_UPDATE_URL}"
    RBBLITZ_LAUNCHER_RELEASES_URL="${RBBLITZ_HOMEPAGE_URL}/releases/latest")

# D1 puts the launcher next to rb_blitz.exe in the build root rather than beside this
# file, because that is the directory the payload snapshot reads (P0.5) and the one a
# user is told to run the game from. Asking for rb_blitz's own output directory keeps
# the two together under any generator or CMAKE_RUNTIME_OUTPUT_DIRECTORY.
set_target_properties(rb_blitz_launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:rb_blitz>")

if(MSVC)
    target_compile_options(rb_blitz_launcher PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(rb_blitz_launcher PRIVATE -Wall -Wextra)
endif()

# imgui's target exposes only its own directory; the backends live beside it, and the
# launcher includes them directly.
target_include_directories(rb_blitz_launcher PRIVATE
    "${REXSDK_DIR}/thirdparty/imgui/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"            # nav.h, schema_view.h, shell.h
    "${CMAKE_CURRENT_SOURCE_DIR}/out/generated"  # settings_table.h (P0.2)
    "${CMAKE_SOURCE_DIR}/src")                   # src/launcher/profile*.h

# The shell includes the generated table, so the table has to exist before it compiles.
add_dependencies(rb_blitz_launcher rb_blitz_launcher_settings)

target_link_libraries(rb_blitz_launcher PRIVATE imgui SDL3::SDL3)

if(WIN32)
    # B8 opens the OS shell (the default browser for the pinned release URL, Explorer for the
    # folder a manual install goes into) through ShellExecuteW. D19's update check is one HTTPS
    # GET, over WinHTTP, for the same reason the installer's helper uses it: it is part of
    # Windows, it honours the machine's proxy configuration, and it needs no extra DLL.
    target_link_libraries(rb_blitz_launcher PRIVATE shell32 winhttp)
endif()

# B7's launch contract, as a test: the argv the launcher hands the game is built from the
# schema, so this target needs the generated table - which is why it lives here rather than
# beside the other launcher tests in the root CMakeLists.txt. No ImGui, no SDL, no process:
# BuildLaunchCommand is pure string work.
if(BUILD_TESTING)
    add_executable(rb_blitz_launcher_launch_tests
        "${CMAKE_SOURCE_DIR}/tests/launcher_launch_tests.cpp"
        src/game_launch.cpp
        src/game_config.cpp
        src/profile_session.cpp
        "${CMAKE_SOURCE_DIR}/src/launcher/profile.cpp"
        "${CMAKE_SOURCE_DIR}/src/launcher/profile_path.cpp"
    )
    target_include_directories(rb_blitz_launcher_launch_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/out/generated")
    add_dependencies(rb_blitz_launcher_launch_tests rb_blitz_launcher_settings)
    # Next to the launcher and the game, like every other test executable this tree builds.
    set_target_properties(rb_blitz_launcher_launch_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:rb_blitz>")
    if(MSVC)
        target_compile_options(rb_blitz_launcher_launch_tests PRIVATE /W4 /utf-8 /EHsc)
    else()
        target_compile_options(rb_blitz_launcher_launch_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME launcher_launch COMMAND rb_blitz_launcher_launch_tests)

    # A3's pad rules, A2's help, and A5's keys: the deadzone, the repeat, the binding table, the
    # binding table A5 makes rebindable - with its chord precedence and its `[nav]` round trip -
    # and the walk from a ring entry to the row the bar names. All dependency-free - no SDL, no
    # ImGui, no window - which is the whole reason the pad's decisions live in pad_nav.cpp and the
    # keys' in nav_bindings.cpp rather than in the glue beside them. It needs the generated table
    # for the same reason the launch tests do.
    add_executable(rb_blitz_launcher_nav_tests
        "${CMAKE_SOURCE_DIR}/tests/launcher_pad_nav_tests.cpp"
        "${CMAKE_SOURCE_DIR}/tests/launcher_help_tests.cpp"
        "${CMAKE_SOURCE_DIR}/tests/launcher_keys_tests.cpp"
        src/pad_nav.cpp
        src/nav_bindings.cpp
        src/schema_view.cpp
        "${CMAKE_SOURCE_DIR}/src/launcher/profile.cpp"
    )
    target_include_directories(rb_blitz_launcher_nav_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/out/generated")
    add_dependencies(rb_blitz_launcher_nav_tests rb_blitz_launcher_settings)
    set_target_properties(rb_blitz_launcher_nav_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:rb_blitz>")
    if(MSVC)
        target_compile_options(rb_blitz_launcher_nav_tests PRIVATE /W4 /utf-8 /EHsc)
    else()
        target_compile_options(rb_blitz_launcher_nav_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME launcher_nav COMMAND rb_blitz_launcher_nav_tests)
endif()
