cargo_build(NAME gstwaylanddisplay)

target_include_directories(gstwaylanddisplay INTERFACE include)

###############
# Extra deps
###

find_package(Libinput REQUIRED)
find_package(Libxkbcommon REQUIRED)
find_package(WaylandServer REQUIRED)
pkg_check_modules(GST_VIDEO REQUIRED IMPORTED_TARGET gstreamer-video-1.0)
pkg_check_modules(GOBJECT REQUIRED IMPORTED_TARGET gobject-2.0 glib-2.0 gmodule-2.0 gio-2.0)

target_link_libraries(gstwaylanddisplay
        INTERFACE
        Libinput::Libinput
        XkbCommon::libxkbcommon
        wayland::server
        PkgConfig::GOBJECT
        PkgConfig::GST_VIDEO)

