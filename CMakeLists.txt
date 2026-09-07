# rb_blitz - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(rb_blitz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(RBBLITZ_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(rb_blitz WIN32 ${RBBLITZ_SOURCES})
else()
    add_executable(rb_blitz ${RBBLITZ_SOURCES})
endif()

rexglue_setup_target(rb_blitz)
