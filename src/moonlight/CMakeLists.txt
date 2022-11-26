# Note that headers are optional, and do not affect add_library, but they will not
# show up in IDEs unless they are listed in add_library.
project(moonlight)

# Optionally glob, but only for CMake 3.12 or later:
file(GLOB HEADER_LIST CONFIGURE_DEPENDS
        moonlight/*.hpp
        reedsolomon/rs.h
        rtsp/parser.hpp)


# Make an automatic library - will be static or dynamic based on user setting
add_library(moonlight)
add_library(wolf::moonlight ALIAS moonlight)

target_sources(moonlight
        PRIVATE
        moonlight.cpp
        reedsolomon/rs.c

        PUBLIC
        ${HEADER_LIST})

# Immutable datastructures
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
target_link_libraries(moonlight PUBLIC immer)
unset(FPHSA_NAME_MISMATCHED)

# Additional algorithms for dealing with containers
FetchContent_Declare(
        range
        GIT_REPOSITORY https://github.com/ericniebler/range-v3.git
        GIT_TAG 0.12.0)
FetchContent_MakeAvailable(range)
target_link_libraries(moonlight PUBLIC range-v3::range-v3)

# Custom parser
FetchContent_Declare(
        peglib
        GIT_REPOSITORY https://github.com/yhirose/cpp-peglib.git
        GIT_TAG v1.8.2)

FetchContent_GetProperties(peglib)
if (NOT peglib_POPULATED)
    FetchContent_Populate(peglib)

    add_library(peglib INTERFACE)
    target_include_directories(peglib INTERFACE ${peglib_SOURCE_DIR})
    target_link_libraries(moonlight PUBLIC peglib)
endif ()

# We need this directory, and users of our library will need it too
target_include_directories(moonlight PUBLIC .)
set_target_properties(moonlight PROPERTIES PUBLIC_HEADER .)
set_target_properties(moonlight PROPERTIES OUTPUT_NAME "moonlight")

# moonlight-common-c dependencies
target_include_directories(moonlight PUBLIC
        ./reedsolomon
        ./rtsp)

find_package(Boost REQUIRED)

# This depends on (header only) boost
target_link_libraries(
        moonlight PUBLIC
        wolf::crypto
        Boost::boost)

# All users of this library will need at least C++17
target_compile_features(moonlight PUBLIC cxx_std_17)
