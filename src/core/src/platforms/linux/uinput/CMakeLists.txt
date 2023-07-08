# Optionally glob, but only for CMake 3.12 or later:
file(GLOB PRIVATE_LIST SRCS input/*.cpp)

# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_uinput)
add_library(wolf::uinput ALIAS wolf_uinput)

find_package(LIBEVDEV)
if (NOT (LIBEVDEV_FOUND))
    message(FATAL_ERROR "Please install libevdev: CMake will Exit")
endif ()

target_link_libraries(wolf_uinput PUBLIC evdev)
target_include_directories(wolf_uinput PUBLIC ${LIBEVDEV_INCLUDE_DIR})

pkg_check_modules(LIBUDEV REQUIRED libudev)
target_link_libraries(wolf_uinput PUBLIC ${LIBUDEV_LIBRARIES})
target_include_directories(wolf_uinput PUBLIC ${LIBUDEV_INCLUDE_DIR})


message(STATUS "Adding input implementation for LINUX")

find_package(ICU 61.0 COMPONENTS uc REQUIRED)
target_link_libraries_system(wolf_uinput PRIVATE ICU::uc)

target_include_directories(wolf_uinput PRIVATE ../../../)
target_sources(wolf_uinput PUBLIC
        "uinput.cpp"
        "keyboard.hpp"
        "uinput.hpp")

FetchContent_Declare(
        eventbus
        GIT_REPOSITORY https://github.com/DeveloperPaul123/eventbus
        GIT_TAG 0.10.1
)
set(EVENTBUS_BUILD_TESTS OFF)
FetchContent_MakeAvailable(eventbus)
target_link_libraries_system(wolf_uinput PUBLIC dp::eventbus)

FetchContent_Declare(
        immer
        GIT_REPOSITORY https://github.com/arximboldi/immer.git
        GIT_TAG e02cbd795e9424a8405a8cb01f659ad61c0cbbc7)
set(immer_BUILD_TESTS OFF)
set(immer_BUILD_EXAMPLES OFF)
set(immer_BUILD_DOCS OFF)
set(immer_BUILD_EXTRAS OFF)

set(FPHSA_NAME_MISMATCHED on) # see: https://github.com/arximboldi/immer/issues/204
FetchContent_MakeAvailable(immer)
target_link_libraries_system(wolf_uinput PUBLIC immer)
unset(FPHSA_NAME_MISMATCHED)

# We need this directory, and users of our library will need it too
target_include_directories(wolf_uinput PUBLIC .)
set_target_properties(wolf_uinput PROPERTIES PUBLIC_HEADER .)
set_target_properties(wolf_uinput PROPERTIES OUTPUT_NAME "input")

find_package(Boost REQUIRED COMPONENTS locale)
include_directories(${Boost_INCLUDE_DIRS})

# This library depends on:
target_link_libraries(wolf_uinput PUBLIC
        ${Boost_LIBRARIES}
        wolf::helpers)

# All users of this library will need at least C++17
target_compile_features(wolf_uinput PUBLIC cxx_std_17)
set_target_properties(wolf_uinput PROPERTIES LINKER_LANGUAGE CXX)

