# Make an automatic library - will be static or dynamic based on user setting

file(GLOB HEADER_LIST CONFIGURE_DEPENDS
        "streaming/*.hpp"
        "streaming/gst-plugin/*.hpp")
file(GLOB SRC_LIST SRCS
        "streaming/*.cpp"
        "streaming/gst-plugin/*.cpp")

add_library(streaming
        ${HEADER_LIST}
        ${SRC_LIST})

# We need this directory, and users of our library will need it too
target_include_directories(streaming PUBLIC .)
set_target_properties(streaming PROPERTIES PUBLIC_HEADER .)
set_target_properties(streaming PROPERTIES OUTPUT_NAME "streaming")

FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 8.1.1)

FetchContent_MakeAvailable(fmtlib)

FetchContent_Declare(
        eventbus
        GIT_REPOSITORY https://github.com/DeveloperPaul123/eventbus
        GIT_TAG 0.10.1
)
set(EVENTBUS_BUILD_TESTS OFF)
FetchContent_MakeAvailable(eventbus)

# Immutable datastructures
FetchContent_Declare(
        immer
        GIT_REPOSITORY https://github.com/arximboldi/immer.git
        GIT_TAG e02cbd795e9424a8405a8cb01f659ad61c0cbbc7
        CONFIGURE_COMMAND ""
        BUILD_COMMAND "")

FetchContent_GetProperties(immer)
if (NOT immer_POPULATED)
    FetchContent_Populate(immer)
    target_include_directories(wolf PUBLIC ${immer_SOURCE_DIR})
endif ()

target_link_libraries(streaming PUBLIC
        fmt::fmt-header-only
        dp::eventbus)

find_package(PkgConfig)

pkg_check_modules(GST REQUIRED gstreamer-1.0)

find_package(GLIB2)
find_package(GObject)
find_package(GStreamer COMPONENTS app audio video)
if (NOT (GSTREAMER_FOUND))
    message(FATAL_ERROR "Please Install Gstreamer Dev: CMake will Exit")
endif ()

# See here for a guide on how to statically link gstreamer:
# https://www.collabora.com/news-and-blog/news-and-events/generate-mininal-gstreamer-build-tailored-to-your-needs.html
target_link_libraries(streaming PUBLIC
        moonlight
        ${GOBJECT_LIBRARIES}
        ${GLIB2_LIBRARIES}
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_BASE_LIBRARIES}
        ${GSTREAMER_APP_LIBRARIES}
        ${GSTREAMER_AUDIO_LIBRARIES}
        ${GSTREAMER_VIDEO_LIBRARIES})

target_include_directories(streaming PUBLIC
        ${GOBJECT_INCLUDE_DIR}
        ${GLIB2_INCLUDE_DIR}
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTREAMER_BASE_INCLUDE_DIRS}
        ${GSTREAMER_APP_INCLUDE_DIRS}
        ${GSTREAMER_AUDIO_INCLUDE_DIRS}
        ${GSTREAMER_VIDEO_INCLUDE_DIRS})


# All users of this library will need at least C++17
target_compile_features(streaming PUBLIC cxx_std_17)
