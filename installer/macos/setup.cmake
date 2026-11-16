# SDK-free settings writer shared by the installed launcher and its headless tests.
set(_macos_setup_root "${CMAKE_CURRENT_LIST_DIR}/../..")
add_library(rb_blitz_macos_setup STATIC
    "${_macos_setup_root}/installer/macos/setup_preferences.cpp"
    "${_macos_setup_root}/src/settings/model.cpp"
    "${_macos_setup_root}/src/settings/schema.cpp"
    "${_macos_setup_root}/src/settings/store.cpp"
    "${_macos_setup_root}/src/settings/paths.cpp"
    "${_macos_setup_root}/src/profile/profile_path.cpp")
target_include_directories(rb_blitz_macos_setup PUBLIC "${_macos_setup_root}/src"
    "${_macos_setup_root}/installer/macos")
target_compile_features(rb_blitz_macos_setup PUBLIC cxx_std_20)
