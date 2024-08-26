cmake_minimum_required(VERSION 3.13...3.24)

# Project name and a few useful settings. Other commands can pick up the results
project(wolf
        VERSION 0.1
        DESCRIPTION "Howling under the Moonlight"
        LANGUAGES CXX)

# Only do these if this is the main project, and not if it is included through add_subdirectory
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)

    # Optionally set things like CMAKE_CXX_STANDARD, CMAKE_POSITION_INDEPENDENT_CODE here

    set(CMAKE_CXX_STANDARD 20)

    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # Let's ensure -std=c++xx instead of -std=g++xx
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Let's nicely support folders in IDEs
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    option(BUILD_SHARED_LIBS "Build libs as shared" ON)

    option(ADD_STACKTRACE_INFO "Adds options in order to get proper stacktrace info on exceptions" ON)
    if (ADD_STACKTRACE_INFO)
        # make sure to -DCMAKE_BUILD_TYPE=RelWithDebInfo)
        set_property(GLOBAL PROPERTY ENABLE_EXPORTS ON)
        set_property(GLOBAL PROPERTY VISIBILITY_INLINES_HIDDEN NO)
        add_link_options("-fno-pie")
        add_compile_options("-fno-inline")
    endif ()

    # set(Boost_USE_STATIC_LIBS ON)

    # Testing only available if this is the main app
    # Note this needs to be done in the main CMakeLists
    # since it calls enable_testing, which must be in the
    # main CMakeLists.
    include(CTest)
endif ()

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif (CCACHE_FOUND)


# FetchContent added in CMake 3.11, downloads during the configure step
include(FetchContent)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/)

include(TargetLinkLibrariesSystem)

add_subdirectory(src/core)

if (UNIX AND NOT APPLE)
    add_subdirectory(src/fake-udev)
endif ()

option(BUILD_MOONLIGHT "Build Moonlight server" ON)
if (BUILD_MOONLIGHT)
    add_subdirectory(src/moonlight-protocol)
    add_subdirectory(src/moonlight-server)
endif (BUILD_MOONLIGHT)

# Testing only available if this is the main app
# Emergency override MODERN_CMAKE_BUILD_TESTING provided as well
if ((CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR MODERN_CMAKE_BUILD_TESTING)
        AND BUILD_TESTING)
    add_subdirectory(tests)
endif ()
