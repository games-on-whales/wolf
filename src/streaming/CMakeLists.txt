# Make an automatic library - will be static or dynamic based on user setting

file(GLOB HEADER_LIST CONFIGURE_DEPENDS
        "streaming/*.hpp"
        "streaming/gst-plugin/*.hpp")
file(GLOB SRC_LIST SRCS
        "streaming/*.cpp"
        "streaming/gst-plugin/*.cpp")

add_library(streaming)
add_library(wolf::streaming ALIAS streaming)

target_sources(streaming
        PRIVATE
        ${SRC_LIST}

        PUBLIC
        ${HEADER_LIST})

# We need this directory, and users of our library will need it too
target_include_directories(streaming PUBLIC .)
set_target_properties(streaming PROPERTIES PUBLIC_HEADER .)
set_target_properties(streaming PROPERTIES OUTPUT_NAME "streaming")

FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 9.1.0)

FetchContent_MakeAvailable(fmtlib)

FetchContent_Declare(
        eventbus
        GIT_REPOSITORY https://github.com/DeveloperPaul123/eventbus
        GIT_TAG 0.10.1
)
set(EVENTBUS_BUILD_TESTS OFF)
FetchContent_MakeAvailable(eventbus)

find_package(PkgConfig)
pkg_check_modules(GST REQUIRED gstreamer-1.0)

find_package(GLIB2)
find_package(GObject)
find_package(GStreamer)
if (NOT (GSTREAMER_FOUND))
    message(FATAL_ERROR "Please Install Gstreamer Dev: CMake will Exit")
endif ()

# See here for a guide on how to statically link gstreamer:
# https://www.collabora.com/news-and-blog/news-and-events/generate-mininal-gstreamer-build-tailored-to-your-needs.html
target_link_libraries(streaming PUBLIC
        wolf::moonlight
        fmt::fmt-header-only
        dp::eventbus
        ${GOBJECT_LIBRARIES}
        ${GLIB2_LIBRARIES}
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_BASE_LIBRARIES})

target_include_directories(streaming PUBLIC
        ${GOBJECT_INCLUDE_DIR}
        ${GLIB2_INCLUDE_DIR}
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTREAMER_BASE_INCLUDE_DIRS})


# All users of this library will need at least C++17
target_compile_features(streaming PUBLIC cxx_std_17)
