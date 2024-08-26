# Optionally glob, but only for CMake 3.12 or later:
file(GLOB PRIVATE_LIST SRCS *.cpp)

# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_uinput)
add_library(wolf::uinput ALIAS wolf_uinput)

target_include_directories(wolf_uinput PRIVATE ../../../)

if (UNIX AND NOT APPLE)
    target_link_libraries(wolf_uinput PUBLIC inputtino::libinputtino)

    find_package(PkgConfig)
    pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
    target_link_libraries(wolf_uinput PUBLIC PkgConfig::LIBEVDEV)

    pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
    target_link_libraries(wolf_uinput PUBLIC PkgConfig::LIBUDEV)
    target_sources(wolf_uinput
            PUBLIC "uinput.hpp"
            PRIVATE ${PRIVATE_LIST})
endif ()

# We need this directory, and users of our library will need it too
target_include_directories(wolf_uinput PUBLIC .)
set_target_properties(wolf_uinput PROPERTIES PUBLIC_HEADER .)
set_target_properties(wolf_uinput PROPERTIES OUTPUT_NAME "input")

find_package(Boost 1.77 REQUIRED COMPONENTS locale)
include_directories(${Boost_INCLUDE_DIRS})

# This library depends on:
target_link_libraries(wolf_uinput PUBLIC
        ${Boost_LIBRARIES}
        wolf::helpers)

# All users of this library will need at least C++17
target_compile_features(wolf_uinput PUBLIC cxx_std_17)
set_target_properties(wolf_uinput PROPERTIES LINKER_LANGUAGE CXX)

