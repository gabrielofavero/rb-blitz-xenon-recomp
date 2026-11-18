add_executable(rb_blitz_preparation_settings_registry_tests
    tests/preparation_settings_registry_tests.cpp
    src/settings/runtime_settings.cpp
    src/settings/runtime_values.cpp
    src/settings/legacy_logging.cpp
    src/settings/graphics_settings.cpp)
# Concrete backend setting definitions are test inputs; production uses the public registry.
target_include_directories(rb_blitz_preparation_settings_registry_tests BEFORE PRIVATE
    src tests "${REXSDK_DIR}/include" "${REXSDK_DIR}/src")
target_link_libraries(rb_blitz_preparation_settings_registry_tests PRIVATE rex::sdk)
target_include_directories(rb_blitz_preparation_settings_registry_tests SYSTEM PRIVATE
    "${REXSDK_DIR}/thirdparty/tomlplusplus/include")
rexglue_configure_target(rb_blitz_preparation_settings_registry_tests)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_preparation_settings_runner "")
if(WIN32)
    list(APPEND _preparation_settings_runner --runner "$<TARGET_FILE:rb_blitz_windows_test_runner>")
endif()
add_test(NAME preparation_settings_registry
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/preparation_settings_registry_tests.py"
        --executable "$<TARGET_FILE:rb_blitz_preparation_settings_registry_tests>"
        ${_preparation_settings_runner})
set_tests_properties(preparation_settings_registry PROPERTIES TIMEOUT 120)
