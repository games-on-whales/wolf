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

# Formatting library
FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 9.1.0)
FetchContent_MakeAvailable(fmtlib)

# Boost for logging
find_package(Boost REQUIRED COMPONENTS log_setup log)
include_directories(${Boost_INCLUDE_DIRS})

target_link_libraries(wolf_helpers INTERFACE
        ${Boost_LIBRARIES}
        fmt::fmt-header-only
        range-v3::range-v3)

# All users of this library will need at least C++17
target_compile_features(wolf_helpers INTERFACE cxx_std_17)
