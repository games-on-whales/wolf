# Make an automatic library - will be static or dynamic based on user setting
add_library(streaming streaming/streaming.cpp ${HEADER_LIST})

# We need this directory, and users of our library will need it too
target_include_directories(streaming PUBLIC .)
set_target_properties(streaming PROPERTIES PUBLIC_HEADER .)
set_target_properties(streaming PROPERTIES OUTPUT_NAME "streaming")

FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 5.3.0)

FetchContent_MakeAvailable(fmtlib)
target_link_libraries(streaming PUBLIC fmt::fmt)

find_package(PkgConfig)

pkg_check_modules(GST REQUIRED gstreamer-1.0)

find_package(GLIB2)
find_package(GObject)
find_package(GStreamer COMPONENTS app audio video pbutils fft)
if (NOT (GSTREAMER_FOUND))
    message(FATAL_ERROR "Please Install Gstreamer Dev: CMake will Exit")
endif ()

target_link_libraries(streaming PUBLIC
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
