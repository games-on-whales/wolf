find_path(WAYLAND_SERVER_INCLUDE_DIR NAMES wayland-server.h)
find_library(WAYLAND_SERVER_LIBRARY NAMES wayland-server libwayland-server)
if(WAYLAND_SERVER_INCLUDE_DIR AND WAYLAND_SERVER_LIBRARY)
    add_library(wayland::server UNKNOWN IMPORTED)

    set_target_properties(
            wayland::server PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${WAYLAND_SERVER_INCLUDE_DIR}"
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION "${WAYLAND_SERVER_LIBRARY}"
    )
endif()