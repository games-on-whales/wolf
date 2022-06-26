# Note that headers are optional, and do not affect add_library, but they will not
# show up in IDEs unless they are listed in add_library.

# Optionally glob, but only for CMake 3.12 or later:
file(GLOB HEADER_LIST CONFIGURE_DEPENDS "${wolf_SOURCE_DIR}/src/moonlight/moonlight/*.hpp")

# Make an automatic library - will be static or dynamic based on user setting
add_library(moonlight moonlight.cpp ${HEADER_LIST})

# We need this directory, and users of our library will need it too
target_include_directories(moonlight PUBLIC .)
set_target_properties(moonlight PROPERTIES PUBLIC_HEADER .)
set_target_properties(moonlight PROPERTIES OUTPUT_NAME "moonlight")

find_package(Boost REQUIRED)

# This depends on (header only) boost
target_link_libraries(
        moonlight PUBLIC
        wolf_crypto
        Boost::boost)

# All users of this library will need at least C++17
target_compile_features(moonlight PUBLIC cxx_std_17)
