# The launcher (docs/plans/launcher-plan.md). This directory holds the launcher's own
# sources, tools and configuration; the executable target itself is added by the
# launcher bring-up work, which extends this file.
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
