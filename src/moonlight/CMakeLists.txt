# Note that headers are optional, and do not affect add_library, but they will not
# show up in IDEs unless they are listed in add_library.
project(moonlight)

# Optionally glob, but only for CMake 3.12 or later:
file(GLOB HEADER_LIST CONFIGURE_DEPENDS "${wolf_SOURCE_DIR}/src/moonlight/moonlight/*.hpp")
set(MOONLIGHT_HEADERS
        ./moonlight-common-c/reedsolomon/rs.c
        ./moonlight-common-c/reedsolomon/rs.h
        ./moonlight-common-c/src/Input.h
        ./moonlight-common-c/src/Rtsp.h
        ./moonlight-common-c/src/RtspParser.c
        ./moonlight-common-c/src/Video.h)

# Make an automatic library - will be static or dynamic based on user setting
add_library(moonlight moonlight.cpp ${HEADER_LIST} ${MOONLIGHT_HEADERS})

# Immutable datastructures
FetchContent_Declare(
        immer
        GIT_REPOSITORY https://github.com/arximboldi/immer.git
        GIT_TAG e02cbd795e9424a8405a8cb01f659ad61c0cbbc7
        CONFIGURE_COMMAND ""
        BUILD_COMMAND "")

# Additional algorithms for dealing with containers
FetchContent_Declare(
        range
        GIT_REPOSITORY https://github.com/ericniebler/range-v3.git
        GIT_TAG 0.12.0
        CONFIGURE_COMMAND ""
        BUILD_COMMAND "")

# Header only, we are only going to fetch it without compiling
FetchContent_GetProperties(range)
if (NOT range_POPULATED)
    FetchContent_Populate(range)
    target_include_directories(moonlight PUBLIC ${range_SOURCE_DIR}/include/)
endif ()

FetchContent_GetProperties(immer)
if (NOT immer_POPULATED)
    FetchContent_Populate(immer)
    target_include_directories(moonlight PUBLIC ${immer_SOURCE_DIR})
endif ()

# We need this directory, and users of our library will need it too
target_include_directories(moonlight PUBLIC .)
set_target_properties(moonlight PROPERTIES PUBLIC_HEADER .)
set_target_properties(moonlight PROPERTIES OUTPUT_NAME "moonlight")

# moonlight-common-c
add_subdirectory(./moonlight-common-c/enet/)
include_directories(
        ./moonlight-common-c/enet/include
        ./moonlight-common-c/reedsolomon)

find_package(Boost REQUIRED)

# This depends on (header only) boost
target_link_libraries(
        moonlight PUBLIC
        wolf_crypto
        Boost::boost)

# All users of this library will need at least C++17
target_compile_features(moonlight PUBLIC cxx_std_17)
