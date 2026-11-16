# Device-only importer. Configuration embedding runs on the Mac build host.
set(_ios_import_root "${CMAKE_CURRENT_LIST_DIR}/../..")
set(RBBLITZ_HOST_IMPORT_DIR "${CMAKE_SOURCE_DIR}/out/build/ios-import" CACHE PATH
    "Native Mac importer build containing rb_blitz_native_embed_config")
set(_ios_embed "${RBBLITZ_HOST_IMPORT_DIR}/rb_blitz_native_embed_config")
if(NOT EXISTS "${_ios_embed}")
    message(FATAL_ERROR "Build the native importer tools first: ${_ios_embed}")
endif()
set(_ios_config "${CMAKE_CURRENT_BINARY_DIR}/ios-import-config")
add_custom_command(
    OUTPUT "${_ios_config}/embedded_config.h"
    COMMAND "${_ios_embed}"
        --pins "${_ios_import_root}/installer/config/pins.toml"
        --game-fingerprints "${_ios_import_root}/config/game_fingerprints.toml"
        --ultimate-fingerprints "${_ios_import_root}/installer/config/ultimate_fingerprints.toml"
        --header "${_ios_config}/embedded_config.h"
        --ispp "${_ios_config}/pins.iss"
        --update-manifest "${_ios_config}/update.toml"
    DEPENDS "${_ios_embed}" "${_ios_import_root}/installer/config/pins.toml"
        "${_ios_import_root}/config/game_fingerprints.toml"
        "${_ios_import_root}/installer/config/ultimate_fingerprints.toml"
    VERBATIM)
add_library(rb_blitz_ios_import_core STATIC
    "${_ios_import_root}/installer/ios/import_session.cpp"
    "${_ios_import_root}/installer/src/game_import_core.cpp"
    "${_ios_import_root}/installer/src/ultimate_import_core.cpp"
    "${_ios_import_root}/installer/src/config.cpp"
    "${_ios_import_root}/installer/src/stfs.cpp"
    "${_ios_import_root}/installer/src/inflate.cpp"
    "${_ios_import_root}/installer/src/zip.cpp"
    "${_ios_import_root}/installer/src/util.cpp"
    "${_ios_import_root}/src/util/game_fingerprint.cpp"
    "${_ios_import_root}/src/util/sha256.cpp"
    "${_ios_config}/embedded_config.h")
target_include_directories(rb_blitz_ios_import_core PUBLIC
    "${_ios_import_root}/installer/src" "${_ios_import_root}/installer/ios"
    "${_ios_import_root}/src" PRIVATE "${_ios_config}")
target_sources(rb_blitz PRIVATE src/platform/ios_importer.mm src/platform/ios_ultimate_download.mm)
set_source_files_properties(src/platform/ios_importer.mm src/platform/ios_ultimate_download.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(rb_blitz PRIVATE rb_blitz_ios_import_core "-framework UniformTypeIdentifiers")
target_compile_definitions(rb_blitz PRIVATE RB_BLITZ_IOS_IMPORTER=1)
