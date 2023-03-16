# Optionally glob, but only for CMake 3.12 or later:
file(GLOB PUBLIC_LIST CONFIGURE_DEPENDS input/*.hpp)
file(GLOB PRIVATE_LIST SRCS input/*.cpp)

# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_input)
add_library(wolf::input ALIAS wolf_input)

# Conditional import for platform based virtual inputs
if (UNIX AND NOT APPLE)

    find_package(LIBEVDEV)
    if (NOT (LIBEVDEV_FOUND))
        message(FATAL_ERROR "Please install libevdev: CMake will Exit")
    endif ()

    target_link_libraries(wolf_input PUBLIC evdev)
    target_include_directories(wolf_input PUBLIC ${LIBEVDEV_INCLUDE_DIR})

    pkg_check_modules(LIBUDEV REQUIRED libudev)
    target_link_libraries(wolf_input PUBLIC ${LIBUDEV_LIBRARIES})
    target_include_directories(wolf_input PUBLIC ${LIBUDEV_INCLUDE_DIR})


    message(STATUS "Adding input implementation for LINUX")
    list(APPEND PRIVATE_LIST
            "platforms/linux/uinput.cpp"
            "platforms/linux/keyboard.hpp"
            "platforms/linux/uinput.hpp")

    find_package(ICU 61.0 COMPONENTS uc REQUIRED)
    target_link_libraries_system(wolf_input PRIVATE ICU::uc)
else () # TODO: other platforms??
    message(WARNING "Missing virtual input implementation for the current platform")
    list(APPEND PRIVATE_LIST "platforms/unknown/input.cpp")
endif ()

target_sources(wolf_input PRIVATE ${PRIVATE_LIST} PUBLIC ${PUBLIC_LIST})

FetchContent_Declare(
        eventbus
        GIT_REPOSITORY https://github.com/DeveloperPaul123/eventbus
        GIT_TAG 0.10.1
)
set(EVENTBUS_BUILD_TESTS OFF)
FetchContent_MakeAvailable(eventbus)
target_link_libraries_system(wolf_input PUBLIC dp::eventbus)

# We need this directory, and users of our library will need it too
target_include_directories(wolf_input PUBLIC .)
set_target_properties(wolf_input PROPERTIES PUBLIC_HEADER .)
set_target_properties(wolf_input PROPERTIES OUTPUT_NAME "input")

find_package(Boost REQUIRED COMPONENTS locale)
include_directories(${Boost_INCLUDE_DIRS})

# This library depends on:
target_link_libraries(wolf_input PUBLIC
        ${Boost_LIBRARIES}
        wolf::helpers
        wolf::moonlight)

# All users of this library will need at least C++17
target_compile_features(wolf_input PUBLIC cxx_std_17)
set_target_properties(wolf_input PROPERTIES LINKER_LANGUAGE CXX)

