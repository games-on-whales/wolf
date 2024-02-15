file(GLOB HEADER_LIST CONFIGURE_DEPENDS
        "crypto/*.hpp"
        "src/*.hpp")
file(GLOB CRYPTO_SRCS SRCS
        "src/*.cpp")

# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_crypto)
add_library(wolf::crypto ALIAS wolf_crypto)

target_sources(wolf_crypto
        PRIVATE
        ${CRYPTO_SRCS}

        PUBLIC
        ${HEADER_LIST})

# We need this directory, and users of our library will need it too
target_include_directories(wolf_crypto PUBLIC .)

#set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

# This depends on OpenSSL
target_link_libraries_system(
        wolf_crypto PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto)

# All users of this library will need at least C++17
target_compile_features(wolf_crypto PUBLIC cxx_std_17)