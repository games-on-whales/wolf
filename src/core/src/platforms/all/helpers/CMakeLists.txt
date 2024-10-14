# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_helpers INTERFACE) # header only library needs to be set as INTERFACE
add_library(wolf::helpers ALIAS wolf_helpers)

# We need this directory, and users of our library will need it too
target_include_directories(wolf_helpers INTERFACE .)
set_target_properties(wolf_helpers PROPERTIES PUBLIC_HEADER .)
target_sources(wolf_helpers INTERFACE helpers/utils.hpp helpers/logger.hpp)

# Additional algorithms for dealing with containers
FetchContent_Declare(
        range
        GIT_REPOSITORY https://github.com/ericniebler/range-v3.git
        GIT_TAG 0.12.0)
FetchContent_MakeAvailable(range)
target_link_libraries_system(wolf_helpers INTERFACE range-v3::range-v3)

# Formatting library
FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.0.1)
FetchContent_MakeAvailable(fmtlib)
target_link_libraries_system(wolf_helpers INTERFACE fmt::fmt-header-only)

# Boost for logging
find_package(Boost 1.77 REQUIRED COMPONENTS log container)
include_directories(${Boost_INCLUDE_DIRS})
target_link_libraries(wolf_helpers INTERFACE ${Boost_LIBRARIES})

FetchContent_Declare(
        boost_json
        GIT_REPOSITORY https://github.com/boostorg/json.git
        GIT_TAG "boost-1.75.0")
set(BOOST_JSON_BUILD_TESTS OFF)
FetchContent_MakeAvailable(boost_json)
target_link_libraries(wolf_helpers INTERFACE Boost::json)

# All users of this library will need at least C++17
target_compile_features(wolf_helpers INTERFACE cxx_std_17)
