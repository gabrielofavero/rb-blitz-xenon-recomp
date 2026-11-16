# Shared native Ultimate installer, using the Windows helper's pins and ZIP reader.
set(_ultimate_root "${CMAKE_CURRENT_LIST_DIR}/../..")
find_package(CURL 7.62 REQUIRED)
add_executable(rb_blitz_native_embed_config "${_ultimate_root}/installer/tools/embed_config.cpp")
set(_ultimate_generated "${CMAKE_CURRENT_BINARY_DIR}/ultimate-config")
add_custom_command(
    OUTPUT "${_ultimate_generated}/embedded_config.h"
    COMMAND rb_blitz_native_embed_config
        --pins "${_ultimate_root}/installer/config/pins.toml"
        --game-fingerprints "${_ultimate_root}/config/game_fingerprints.toml"
        --ultimate-fingerprints "${_ultimate_root}/installer/config/ultimate_fingerprints.toml"
        --header "${_ultimate_generated}/embedded_config.h"
        --ispp "${_ultimate_generated}/pins.iss"
        --update-manifest "${_ultimate_generated}/update.toml"
    DEPENDS rb_blitz_native_embed_config
        "${_ultimate_root}/installer/config/pins.toml"
        "${_ultimate_root}/config/game_fingerprints.toml"
        "${_ultimate_root}/installer/config/ultimate_fingerprints.toml"
    VERBATIM)
add_library(rb_blitz_ultimate_import_core STATIC
    "${_ultimate_root}/installer/src/ultimate_import_core.cpp"
    "${_ultimate_root}/installer/src/config.cpp"
    "${_ultimate_root}/installer/src/zip.cpp"
    "${_ultimate_generated}/embedded_config.h")
target_include_directories(rb_blitz_ultimate_import_core PRIVATE "${_ultimate_generated}")
target_link_libraries(rb_blitz_ultimate_import_core PUBLIC rb_blitz_game_import_core CURL::libcurl)
target_compile_features(rb_blitz_ultimate_import_core PUBLIC cxx_std_20)
