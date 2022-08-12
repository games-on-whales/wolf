# Note that headers are optional, and do not affect add_library, but they will not
# show up in IDEs unless they are listed in add_library.

# Optionally glob, but only for CMake 3.12 or later:
file(GLOB HEADER_LIST CONFIGURE_DEPENDS "${wolf_SOURCE_DIR}/src/crypto/crypto/*.hpp" "${wolf_SOURCE_DIR}/src/crypto/src/*.hpp")
file(GLOB CRYPTO_LIST SRCS "${wolf_SOURCE_DIR}/src/crypto/src/*.cpp")
# set(HEADER_LIST "${wolf_SOURCE_DIR}/include/moonlight/moonlight.hpp")

# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_crypto src/crypto.cpp ${CRYPTO_LIST} ${HEADER_LIST})

# We need this directory, and users of our library will need it too
target_include_directories(wolf_crypto PUBLIC .)

#set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

# This depends on OpenSSL
target_link_libraries(
        wolf_crypto PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto)

# All users of this library will need at least C++17
target_compile_features(wolf_crypto PUBLIC cxx_std_17)