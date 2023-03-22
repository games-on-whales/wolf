# Make an automatic library - will be static or dynamic based on user setting

file(GLOB HEADER_LIST CONFIGURE_DEPENDS "streaming/gst-plugin/*.hpp")
file(GLOB SRC_LIST SRCS "streaming/gst-plugin/*.cpp")

add_library(streaming)
add_library(wolf::streaming ALIAS streaming)

# We need this directory, and users of our library will need it too
target_include_directories(streaming PUBLIC .)
set_target_properties(streaming PROPERTIES PUBLIC_HEADER .)
set_target_properties(streaming PROPERTIES OUTPUT_NAME "streaming")

FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 9.1.0)
FetchContent_MakeAvailable(fmtlib)
target_link_libraries_system(streaming PUBLIC fmt::fmt-header-only)

FetchContent_Declare(
        eventbus
        GIT_REPOSITORY https://github.com/DeveloperPaul123/eventbus
        GIT_TAG 0.10.1
)
set(EVENTBUS_BUILD_TESTS OFF)
FetchContent_MakeAvailable(eventbus)
target_link_libraries_system(streaming PUBLIC dp::eventbus)

find_package(PkgConfig)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-base-1.0 gstreamer-app-1.0)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0)

if (NOT (GSTREAMER_FOUND))
    message(FATAL_ERROR "Please Install Gstreamer Dev: CMake will Exit")
endif ()

if (LINK_RUST_WAYLAND AND UNIX AND NOT APPLE)
    target_link_libraries_system(streaming PUBLIC gstwaylanddisplay)
    list(APPEND SRC_LIST streaming/platforms/linux/wayland-display.cpp)
else ()
    message(WARNING "Missing virtual display implementation for this platform")
    list(APPEND SRC_LIST streaming/platforms/unknown/no-virtual-display.cpp)
endif ()

list(APPEND HEADER_LIST streaming/virtual-display.hpp streaming/streaming.hpp streaming/data-structures.hpp)
list(APPEND SRC_LIST streaming/streaming.cpp)

target_sources(streaming
        PRIVATE
        ${SRC_LIST}

        PUBLIC
        ${HEADER_LIST})

target_link_libraries(streaming PUBLIC
        PkgConfig::GLIB2
        PkgConfig::GSTREAMER
        wolf::moonlight
        wolf::helpers
        wolf::input)


# All users of this library will need at least C++17
target_compile_features(streaming PUBLIC cxx_std_17)
