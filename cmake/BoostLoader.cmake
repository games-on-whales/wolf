set(BOOST_VERSION 1.87.0)

set(BOOST_COMPONENTS
        log
        container
        locale
        process
        # Align needed by boost::json
        align
        json
        system
)
find_package(Boost ${BOOST_VERSION} COMPONENTS ${BOOST_COMPONENTS} QUIET)
if (NOT Boost_FOUND)
    message(STATUS "Boost (or some required components) not found, falling back to FetchContent instead")

    set(BOOST_INCLUDE_LIBRARIES ${BOOST_COMPONENTS})
    set(BOOST_ENABLE_CMAKE ON)
    FetchContent_Declare(
            Boost
            URL "https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.xz"
    )
    FetchContent_MakeAvailable(Boost)

    set(Boost_FOUND TRUE)
    set(Boost_INCLUDE_DIRS "$<BUILD_INTERFACE:${Boost_SOURCE_DIR}/libs/headers/include>")
    set(Boost_LIBRARIES "")  # cmake-lint: disable=C0103
    foreach (component ${BOOST_COMPONENTS})
        list(APPEND Boost_LIBRARIES "Boost::${component}")
    endforeach ()
endif ()
include_directories(${Boost_INCLUDE_DIRS})