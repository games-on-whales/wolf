# Testing library
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.3.2
)

FetchContent_MakeAvailable(Catch2)

# Tests need to be added as executables first
add_executable(wolftests main.cpp)

set(SRC_LIST
        main.cpp
        testControl.cpp
        testCrypto.cpp
        testGSTPlugin.cpp
        testMoonlight.cpp
        testRTSP.cpp)

if (UNIX AND NOT APPLE)
    option(TEST_RUST_WAYLAND "Enable custom wayland test" ON)
    if (TEST_RUST_WAYLAND)
        list(APPEND SRC_LIST "platforms/linux/wayland-display.cpp")
    endif ()

    option(TEST_NVIDIA "Enable Nvidia tests" ON)
    if (TEST_NVIDIA)
        list(APPEND SRC_LIST "platforms/linux/nvidia.cpp")
    endif ()

    option(TEST_VIRTUAL_INPUT "Enable libinput test" ON)
    if (TEST_VIRTUAL_INPUT)
        find_package(PkgConfig)
        pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
        target_link_libraries(wolftests PRIVATE PkgConfig::LIBINPUT)

        if (BUILD_FAKE_UDEV_CLI)
            list(APPEND SRC_LIST "platforms/linux/fake-udev.cpp")
            target_link_libraries(wolftests PRIVATE fake-udev::lib)
        endif ()
        list(APPEND SRC_LIST
                "platforms/linux/libinput.h"
                "platforms/linux/input.cpp")
    endif ()
endif ()

option(TEST_DOCKER "Enable docker tests" ON)
if (TEST_DOCKER)
    list(APPEND SRC_LIST "docker/testDocker.cpp")
endif ()

option(TEST_EXCEPTIONS "Enable exceptions tests" ON)
if (TEST_EXCEPTIONS)
    list(APPEND SRC_LIST testExceptions.cpp)
endif ()

option(TEST_SDL "Enabled SDL tests" ON)
if (TEST_SDL)
    option(SDL_CUSTOM_SRC "Use a custom SDL source location (useful to better debug)" OFF)
    if (SDL_CUSTOM_SRC)
        SET(SDL_TEST OFF)
        add_subdirectory(${SDL_CUSTOM_SRC} ${CMAKE_CURRENT_BINARY_DIR}/sdl EXCLUDE_FROM_ALL)
    else ()
        find_package(SDL2 REQUIRED CONFIG REQUIRED COMPONENTS SDL2)
    endif ()

    target_link_libraries(wolftests PRIVATE SDL2::SDL2)
    list(APPEND SRC_LIST "testJoypads.cpp")
endif ()

target_sources(wolftests PRIVATE ${SRC_LIST})

# I'm using C++17 in the test
target_compile_features(wolftests PRIVATE cxx_std_17)

# Should be linked to the main library, as well as the Catch2 testing library
target_link_libraries_system(wolftests PRIVATE
        wolf::runner
        Catch2::Catch2WithMain)

## Test assets
configure_file(assets/config.v2.toml ${CMAKE_CURRENT_BINARY_DIR}/config.v2.toml COPYONLY)

# See: https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(wolftests)
