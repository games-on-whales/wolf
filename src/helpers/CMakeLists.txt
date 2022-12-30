# Optionally glob, but only for CMake 3.12 or later:
file(GLOB HEADER_LIST CONFIGURE_DEPENDS helpers/*.hpp)

# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_helpers)
add_library(wolf::helpers ALIAS wolf_helpers)

target_sources(wolf_helpers PUBLIC ${HEADER_LIST})

# We need this directory, and users of our library will need it too
target_include_directories(wolf_helpers PUBLIC .)
set_target_properties(wolf_helpers PROPERTIES PUBLIC_HEADER .)
set_target_properties(wolf_helpers PROPERTIES OUTPUT_NAME "helpers")

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

target_link_libraries(wolf_helpers PUBLIC
        ${Boost_LIBRARIES}
        fmt::fmt-header-only
        range-v3::range-v3)

# All users of this library will need at least C++17
target_compile_features(wolf_helpers PUBLIC cxx_std_17)
set_target_properties(wolf_helpers PROPERTIES LINKER_LANGUAGE CXX)
