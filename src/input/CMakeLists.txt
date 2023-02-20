# Optionally glob, but only for CMake 3.12 or later:
file(GLOB HEADER_LIST CONFIGURE_DEPENDS input/*.hpp)
file(GLOB SRC_LIST SRCS input/*.cpp)

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

    message(STATUS "Adding input implementation for LINUX")
    list(APPEND SRC_LIST "platforms/linux/uinput.cpp")
    list(APPEND HEADER_LIST "platforms/linux/keyboard.hpp" "platforms/linux/uinput.hpp")

    find_package(ICU 61.0 COMPONENTS uc REQUIRED)
    target_link_libraries(wolf_input PRIVATE ICU::uc)
else () # TODO: other platforms??
    message(WARNING "Missing virtual input implementation for the current platform")
    list(APPEND SRC_LIST "platforms/unknown/input.cpp")
endif ()

target_sources(wolf_input PRIVATE ${SRC_LIST} PUBLIC ${HEADER_LIST})

FetchContent_Declare(
        eventbus
        GIT_REPOSITORY https://github.com/DeveloperPaul123/eventbus
        GIT_TAG 0.10.1
)
set(EVENTBUS_BUILD_TESTS OFF)
FetchContent_MakeAvailable(eventbus)

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
        wolf::moonlight
        dp::eventbus)

# All users of this library will need at least C++17
target_compile_features(wolf_input PUBLIC cxx_std_17)
set_target_properties(wolf_input PROPERTIES LINKER_LANGUAGE CXX)

