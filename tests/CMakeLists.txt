# Testing library
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.3.2
)

FetchContent_MakeAvailable(Catch2)

# Tests need to be added as executables first
add_executable(wolftests main.cpp)

file(GLOB SRC_LIST SRCS *.cpp)

if (UNIX AND NOT APPLE)
    option(TEST_UINPUT "Enable uinput test" ON) # uinput doesn't work in GA
    if (TEST_UINPUT)
        list(APPEND SRC_LIST "platforms/linux/input.cpp")
        if (BUILD_FAKE_UDEV_CLI)
            list(APPEND SRC_LIST "platforms/linux/fake-udev.cpp")
            target_link_libraries(wolftests PRIVATE fake-udev::lib)
        endif ()
    endif ()

    option(TEST_RUST_WAYLAND "Enable custom wayland test" ON)
    if (TEST_RUST_WAYLAND)
        list(APPEND SRC_LIST "platforms/linux/wayland-display.cpp")
    endif ()

    option(TEST_NVIDIA "Enable Nvidia tests" ON)
    if (TEST_NVIDIA)
        list(APPEND SRC_LIST "platforms/linux/nvidia.cpp")
    endif ()

    option(TEST_SDL "Enabled SDL tests" OFF) # TODO: SDL does pick up my joypad but it doesn't detect gyro/acceleration
    if (TEST_SDL)
        find_package(SDL2 REQUIRED)
        target_include_directories(wolftests PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(wolftests PRIVATE SDL2::SDL2)
        list(APPEND SRC_LIST "platforms/linux/sdl-test-joypad.cpp")
    endif ()
endif ()

option(TEST_DOCKER "Enable docker tests" ON)
if (TEST_DOCKER)
    list(APPEND SRC_LIST "docker/testDocker.cpp")
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
