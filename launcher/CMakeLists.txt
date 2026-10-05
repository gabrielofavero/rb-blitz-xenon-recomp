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

add_custom_command(
    OUTPUT "${RBBLITZ_SETTINGS_HEADER}"
    COMMAND $<TARGET_FILE:rb_blitz_embed_settings>
            --settings "${RBBLITZ_SETTINGS_TOML}"
            --header "${RBBLITZ_SETTINGS_HEADER}"
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
    src/game_config.cpp
    src/general_report.cpp
    src/general_tab.cpp
    src/install_ultimate.cpp
    src/nav.cpp
    src/path_validate.cpp
    src/profile_session.cpp
    src/profile_ui.cpp
    src/row_ui.cpp
    src/schema_view.cpp
    src/settings_edit.cpp
    src/shell.cpp
    src/ultimate_state.cpp
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

target_compile_features(rb_blitz_launcher PRIVATE cxx_std_20)

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
    # folder a manual install goes into) through ShellExecuteW.
    target_link_libraries(rb_blitz_launcher PRIVATE shell32)
endif()
