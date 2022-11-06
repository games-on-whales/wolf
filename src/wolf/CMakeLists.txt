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
set(EVENTBUS_BUILD_TESTS OFF)
FetchContent_MakeAvailable(eventbus)

# HTTP server library
FetchContent_Declare(
        simplewebserver
        GIT_REPOSITORY https://gitlab.com/eidheim/Simple-Web-Server.git
        GIT_TAG bdb1057)
FetchContent_MakeAvailable(simplewebserver)
target_link_libraries(wolf PUBLIC simple-web-server)

# TOML parser (config file)
FetchContent_Declare(
        toml
        GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
        GIT_TAG v3.7.1)
set(TOML11_COLORIZE_ERROR_MESSAGE, ON)
FetchContent_MakeAvailable(toml)
target_link_libraries(wolf PUBLIC toml11::toml11)

# Enet
set(CMAKE_PROJECT_INCLUDE_BEFORE "${PROJECT_SOURCE_DIR}/cmake/EnableCMP0048.cmake")
FetchContent_Declare(
        enet
        GIT_REPOSITORY https://github.com/cgutman/enet
        GIT_TAG 4cde9cc3dcc5c30775a80da1de87f39f98672a31)
FetchContent_MakeAvailable(enet)
target_link_libraries(wolf PUBLIC enet)
unset(CMAKE_PROJECT_INCLUDE_BEFORE)

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
        wolf::moonlight
        wolf::streaming
        ${Boost_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${CMAKE_THREAD_LIBS_INIT}
        fmt::fmt-header-only
        dp::eventbus
        #-static # enable to statically link all libraries
)

# The following is needed in order to import this into the test suite
set_property(TARGET wolf PROPERTY ENABLE_EXPORTS 1)