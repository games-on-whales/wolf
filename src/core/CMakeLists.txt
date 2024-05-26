add_library(wolf_core)
add_library(wolf::core ALIAS wolf_core)

target_include_directories(wolf_core PUBLIC src)

# Platform agnostic code
add_subdirectory(src/platforms/all/helpers)
target_link_libraries(wolf_core PUBLIC wolf::helpers)

add_subdirectory(src/platforms/all/docker)
target_link_libraries(wolf_core PUBLIC wolf::docker)

file(GLOB CORE_HEADERS CONFIGURE_DEPENDS src/core/*.hpp)
set(CORE_SRC "")

find_package(PkgConfig)
option(LINK_RUST_WAYLAND "Link to our custom Rust wayland compositor" ON)
if (LINK_RUST_WAYLAND AND UNIX AND NOT APPLE)
    pkg_check_modules(libgstwaylanddisplay REQUIRED IMPORTED_TARGET libgstwaylanddisplay)
    target_link_libraries(wolf_core PUBLIC PkgConfig::libgstwaylanddisplay)
endif ()

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
target_link_libraries_system(wolf_core PUBLIC immer)
unset(FPHSA_NAME_MISMATCHED)

pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-base-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0)

if (NOT (GSTREAMER_FOUND))
    message(FATAL_ERROR "Please Install Gstreamer Dev: CMake will Exit")
endif ()
target_link_libraries(wolf_core PUBLIC
        PkgConfig::GSTREAMER
        PkgConfig::GLIB2)

# Platform dependent code
if (UNIX AND NOT APPLE)
    add_subdirectory(src/platforms/linux/pulseaudio)
    target_link_libraries(wolf_core PUBLIC wolf::audio)

    FetchContent_Declare(
            inputtino
            GIT_REPOSITORY https://github.com/games-on-whales/inputtino.git
            GIT_TAG cf68026)
    FetchContent_MakeAvailable(inputtino)

    add_subdirectory(src/platforms/linux/uinput)
    target_link_libraries(wolf_core PUBLIC wolf::uinput)

    if (LINK_RUST_WAYLAND)
        list(APPEND CORE_SRC src/platforms/linux/virtual-display/wayland-display.cpp)
    else ()
        message(WARNING "Missing virtual display implementation for this platform")
        list(APPEND CORE_SRC platforms/unknown/no-virtual-display.cpp)
    endif ()
else ()
    file(GLOB CORE_SRC SRCS platforms/unknown/*.cpp)
endif ()

target_sources(wolf_core PUBLIC ${CORE_HEADERS} PRIVATE ${CORE_SRC})

# All users of this library will need at least C++17
target_compile_features(wolf_core PUBLIC cxx_std_17)
set_target_properties(wolf_core PROPERTIES LINKER_LANGUAGE CXX)
