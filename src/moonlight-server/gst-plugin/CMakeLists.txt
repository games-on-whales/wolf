cmake_minimum_required(VERSION 3.13...3.24)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

project(wolf-gst-plugins
        VERSION 0.1
        DESCRIPTION "A collection of GStreamer plugins for the Wolf project"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

add_library(tracy SHARED ./gsttracy.cpp ./gsttracy.hpp)

######################
# Tracy

include(FetchContent)
FetchContent_Declare(tracy_client
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.10
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
)
set(TRACY_ENABLE ON)
set(TRACY_ON_DEMAND ON)
FetchContent_MakeAvailable(tracy_client)
set_property(TARGET TracyClient PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(tracy PUBLIC TracyClient)


######################
# GSTREAMER

find_package(PkgConfig)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0)

if (NOT (GSTREAMER_FOUND))
    message(FATAL_ERROR "Please Install Gstreamer Dev: CMake will Exit")
endif ()
target_link_libraries(tracy PUBLIC PkgConfig::GSTREAMER PkgConfig::GLIB2)

######################