# Adapted from: https://github.com/jpd002/Play-Dependencies/blob/master/cmake-modules/FindLIBEVDEV.cmake
# Find freedesktop.org's libevdev headers and library
# This module defines the following variables:
#
#  LIBEVDEV_FOUND		- true if libevdev header and library was found
#  LIBEVDEV_INCLUDE_DIR	- include path for libevdev
#  LIBEVDEV_LIBRARY	- library path for libevdev
#

find_package(PkgConfig)
pkg_check_modules(LIBEVDEV libevdev>=1.0)

SET(LIBEVDEV_SEARCH_PATHS
        /usr/local
        /usr
        )

FIND_PATH(LIBEVDEV_INCLUDE_DIR libevdev/libevdev.h
        HINTS $ENV{EVDEVDIR}
        PATH_SUFFIXES include/libevdev-1.0 include/
        PATHS ${EVDEV_SEARCH_PATHS}
        )

FIND_LIBRARY(LIBEVDEV_LIBRARY
        NAMES libevdev.a evdev
        HINTS
        $ENV{EVDEVDIR}
        PATH_SUFFIXES ${PATH_SUFFIXES}
        PATHS ${EVDEV_SEARCH_PATHS}
        )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBEVDEV REQUIRED_VARS LIBEVDEV_LIBRARY LIBEVDEV_INCLUDE_DIR)

mark_as_advanced(LIBEVDEV_INCLUDE_DIR)