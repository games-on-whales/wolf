# Testing library
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.0.0-preview3
)

FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 5.3.0)

FetchContent_MakeAvailable(fmtlib)

find_package(Boost REQUIRED COMPONENTS log_setup log)
include_directories(${Boost_INCLUDE_DIRS})

# Tests need to be added as executables first
add_executable(wolflib testMoonlight.cpp testCrypto.cpp)

target_include_directories(wolflib PUBLIC ./ ../src/wolf)

# I'm using C++17 in the test
target_compile_features(wolflib PRIVATE cxx_std_17)

# Should be linked to the main library, as well as the Catch2 testing library
target_link_libraries(wolflib PRIVATE
        ${Boost_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${CMAKE_THREAD_LIBS_INIT}
        moonlight
        wolf
        wolf_crypto
        fmt::fmt
        Catch2::Catch2WithMain)

## Test assets
configure_file(assets/config.json ${CMAKE_CURRENT_BINARY_DIR}/config.json COPYONLY)
configure_file(assets/server_info_response.xml ${CMAKE_CURRENT_BINARY_DIR}/server_info_response.xml COPYONLY)

# See: https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(wolflib)
