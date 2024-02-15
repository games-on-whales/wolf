option(BUILD_FAKE_UDEV_CLI "Build fake-udev CLI executable" ON)
if (BUILD_FAKE_UDEV_CLI)
    message(STATUS "Building fake-udev CLI tool")

    add_library(fake_udev_lib STATIC)
    add_library(fake-udev::lib ALIAS fake_udev_lib)
    target_include_directories(fake_udev_lib PUBLIC .)
    target_sources(fake_udev_lib
            PUBLIC fake-udev/fake-udev.hpp fake-udev/MurmurHash2.h
            PRIVATE fake-udev/MurmurHash2.cpp
    )
    target_compile_features(fake_udev_lib PRIVATE cxx_std_17)

    # Executable
    add_executable(fake-udev fake-udev-cli.cpp)
    target_link_libraries(fake-udev fake-udev::lib -static)
    target_compile_features(fake-udev PRIVATE cxx_std_17)
endif ()