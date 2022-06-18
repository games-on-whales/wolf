add_executable(wolf wolf.cpp)
target_compile_features(wolf PRIVATE cxx_std_17)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Formatting library
FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 5.3.0)

FetchContent_MakeAvailable(fmtlib)

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

# Some libraries don't work with FetchContent_Declare out of the box
# We have to manually git clone them and adding them into `libraries`
target_include_directories(wolf PRIVATE libraries .)

target_link_libraries(
        wolf PRIVATE
        moonlight
        streaming
        ${Boost_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${CMAKE_THREAD_LIBS_INIT}
        fmt::fmt
)

if (UNIX AND NOT APPLE)
    add_compile_definitions(WOLF_PLATFORM="linux")

    find_package(PipeWire)
    if (PipeWire_FOUND)
        target_include_directories(wolf PRIVATE platforms/linux)
        target_link_libraries(wolf PRIVATE PipeWire::PipeWire)
    endif ()
endif ()