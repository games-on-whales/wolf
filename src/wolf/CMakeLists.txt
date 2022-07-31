add_executable(wolf wolf.cpp)
target_compile_features(wolf PRIVATE cxx_std_17)

##############################
# External deps via GIT
####

# Formatting library
FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 8.1.1)
FetchContent_MakeAvailable(fmtlib) # Download and build

FetchContent_Declare(
        eventbus
        GIT_REPOSITORY https://github.com/DeveloperPaul123/eventbus
        GIT_TAG 0.10.1
)
FetchContent_MakeAvailable(eventbus)

# HTTP server library
FetchContent_Declare(
        simplewebserver
        GIT_REPOSITORY https://gitlab.com/eidheim/Simple-Web-Server.git
        GIT_TAG bdb1057
        CONFIGURE_COMMAND ""
        BUILD_COMMAND "")

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
    target_include_directories(wolf PUBLIC ${range_SOURCE_DIR}/include/)
endif ()

FetchContent_GetProperties(simplewebserver)
if (NOT simplewebserver_POPULATED)
    FetchContent_Populate(simplewebserver)
    target_include_directories(wolf PUBLIC ${simplewebserver_SOURCE_DIR})
endif ()

FetchContent_GetProperties(immer)
if (NOT immer_POPULATED)
    FetchContent_Populate(immer)
    target_include_directories(wolf PUBLIC ${immer_SOURCE_DIR})
endif ()

##############################

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_package(Boost
        REQUIRED COMPONENTS
        log_setup
        log
        stacktrace_basic)
include_directories(${Boost_INCLUDE_DIRS})

### Boost stacktrace exception handling
# adapted from https://github.com/jschueller/boost-stacktrace-example/blob/master/CMakeLists.txt
# also see: https://www.boost.org/doc/libs/develop/doc/html/stacktrace/configuration_and_build.html
find_library(BOOST_STACKTRACE_BACKTRACE_LIBRARY NAMES boost_stacktrace_backtrace)
if (UNIX)
    message(STATUS "Using Boost::stacktrace/addr2line")
    add_definitions(-D_GNU_SOURCE=1)
    target_compile_definitions(wolf PRIVATE BOOST_STACKTRACE_USE_ADDR2LINE)
    target_link_libraries(wolf PRIVATE dl)
elseif (MINGW AND BOOST_STACKTRACE_BACKTRACE_LIBRARY)
    message(STATUS "Using Boost::stacktrace/backtrace")
    target_compile_definitions(wolf PRIVATE BOOST_STACKTRACE_USE_BACKTRACE)
    target_link_libraries(wolf PRIVATE boost_stacktrace_backtrace backtrace)
else ()
    message(STATUS "Using Boost::stacktrace/basic")
endif ()

target_include_directories(wolf PUBLIC .)

target_link_libraries(
        wolf PUBLIC
        moonlight
        streaming
        ${Boost_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${CMAKE_THREAD_LIBS_INIT}
        fmt::fmt
        dp::eventbus
)

# The following is needed in order to import this into the test suite
set_property(TARGET wolf PROPERTY ENABLE_EXPORTS 1)


if (UNIX AND NOT APPLE)
    add_compile_definitions(WOLF_PLATFORM="linux")

    find_package(PipeWire)
    if (PipeWire_FOUND)
        target_include_directories(wolf PRIVATE platforms/linux)
        target_link_libraries(wolf PRIVATE PipeWire::PipeWire)
    endif ()
endif ()