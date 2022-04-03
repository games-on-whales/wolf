# Note that headers are optional, and do not affect add_library, but they will not
# show up in IDEs unless they are listed in add_library.

# Optionally glob, but only for CMake 3.12 or later:
file(GLOB HEADER_LIST CONFIGURE_DEPENDS "${wolf_SOURCE_DIR}/include/moonlight/*.hpp")
file(GLOB CRYPTO_LIST SRCS "${wolf_SOURCE_DIR}/src/crypto/*.cpp")
# set(HEADER_LIST "${wolf_SOURCE_DIR}/include/moonlight/protocol.hpp")

# Make an automatic library - will be static or dynamic based on user setting
add_library(moonlight moonlight.cpp crypto.cpp ${CRYPTO_LIST} ${HEADER_LIST})

# We need this directory, and users of our library will need it too
target_include_directories(moonlight PUBLIC ../include crypto)

find_package(OpenSSL REQUIRED)

# This depends on (header only) boost
target_link_libraries(
        moonlight PUBLIC
        Boost::boost
        OpenSSL::SSL
        OpenSSL::Crypto)

# All users of this library will need at least C++17
target_compile_features(moonlight PUBLIC cxx_std_17)

# IDEs should put the headers in a nice place
source_group(
        TREE "${PROJECT_SOURCE_DIR}/include"
        PREFIX "Header Files"
        FILES ${HEADER_LIST})
