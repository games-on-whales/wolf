add_library(wolf_runner)
add_library(wolf::runner ALIAS wolf_runner)

find_package(PkgConfig)

##############################
# External deps via GIT
####

# Formatting library
FetchContent_Declare(
        fmtlib
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.0.1)
FetchContent_MakeAvailable(fmtlib) # Download and build
target_link_libraries_system(wolf_runner PUBLIC fmt::fmt-header-only)

# Event bus
FetchContent_Declare(
        eventbus
        GIT_REPOSITORY https://github.com/games-on-whales/eventbus
        GIT_TAG abb3a48
)
set(EVENTBUS_BUILD_TESTS OFF)
FetchContent_MakeAvailable(eventbus)
target_link_libraries_system(wolf_runner PUBLIC dp::eventbus)

# TOML
FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)
target_link_libraries(wolf_runner PUBLIC tomlplusplus::tomlplusplus)

# Serialization
FetchContent_Declare(
        reflect-cpp
        GIT_REPOSITORY https://github.com/getml/reflect-cpp
        GIT_TAG 54c2a84
)
set(REFLECTCPP_BSON OFF)
set(REFLECTCPP_CBOR OFF)
set(REFLECTCPP_FLEXBUFFERS OFF)
set(REFLECTCPP_MSGPACK OFF) # Only works with vcpkg, doing it manually
set(REFLECTCPP_TOML OFF)
set(REFLECTCPP_XML OFF)
set(REFLECTCPP_YAML OFF)

FetchContent_MakeAvailable(reflect-cpp)
target_link_libraries(wolf_runner PUBLIC reflectcpp)


# HTTP server library
FetchContent_Declare(
        simplewebserver
        GIT_REPOSITORY https://gitlab.com/eidheim/Simple-Web-Server.git
        GIT_TAG bdb1057)
FetchContent_MakeAvailable(simplewebserver)
target_link_libraries_system(wolf_runner PUBLIC simple-web-server)

# mDNS
FetchContent_Declare(
        mdns_cpp
        GIT_REPOSITORY https://github.com/gocarlos/mdns_cpp.git)
FetchContent_MakeAvailable(mdns_cpp)
target_link_libraries_system(wolf_runner PUBLIC mdns_cpp::mdns_cpp)


# Enet
set(CMAKE_PROJECT_INCLUDE_BEFORE "${PROJECT_SOURCE_DIR}/cmake/EnableCMP0048.cmake")
FetchContent_Declare(
        enet
        GIT_REPOSITORY https://github.com/cgutman/enet
        GIT_TAG 47e42dbf422396ce308a03b5a95ec056f0f0180c)
FetchContent_MakeAvailable(enet)
target_link_libraries_system(wolf_runner PUBLIC enet)
unset(CMAKE_PROJECT_INCLUDE_BEFORE)

# Stacktrace
FetchContent_Declare(
        cpptrace
        GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
        GIT_TAG 448c325
)

pkg_check_modules(LIBUNWIND QUIET libunwind)
if (LIBUNWIND_FOUND)
    set(CPPTRACE_UNWIND_WITH_LIBUNWIND ON)
else ()
    message(WARNING "Missing libunwind, stacktraces will not be available.")
endif ()
FetchContent_MakeAvailable(cpptrace)

##############################

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_package(Boost 1.77 REQUIRED COMPONENTS log locale)
include_directories(${Boost_INCLUDE_DIRS})

target_link_libraries(
        wolf_runner PUBLIC
        wolf::moonlight
        wolf::helpers
        wolf::core
        cpptrace::cpptrace
        ${Boost_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${CMAKE_THREAD_LIBS_INIT})

target_include_directories(wolf_runner PUBLIC .)

file(GLOB HEADER_LIST CONFIGURE_DEPENDS */*.hpp)
file(GLOB SRC_LIST SRCS
        api/*.cpp
        gst-plugin/*.cpp
        control/*.cpp
        rest/*.cpp
        rtp/*.cpp
        rtsp/*.cpp
        runners/*.cpp
        state/*.cpp
        streaming/*.cpp)

if (UNIX AND NOT APPLE)
    message(STATUS "Adding input implementation for LINUX")

    find_package(ICU 61.0 COMPONENTS uc REQUIRED) # brought by libboost-locale-dev
    target_link_libraries_system(wolf_runner PRIVATE ICU::uc)

    list(APPEND SRC_LIST platforms/input_linux.cpp)
    pkg_check_modules(LIBDRM IMPORTED_TARGET libdrm)
    pkg_check_modules(LIBPCI IMPORTED_TARGET libpci)

    if (LIBDRM_FOUND AND LIBPCI_FOUND)
        list(APPEND SRC_LIST platforms/hw_linux.cpp)
        target_link_libraries(
                wolf_runner
                PUBLIC
                PkgConfig::LIBDRM
                PkgConfig::LIBPCI
        )
    else ()
        message(WARNING "Missing libdrm or libpci, automatic GPU recognition will not work with this build.")
        list(APPEND SRC_LIST platforms/hw_unknown.cpp)
    endif ()
else ()
    list(APPEND SRC_LIST platforms/hw_unknown.cpp)
endif ()

target_sources(wolf_runner
        PRIVATE
        ${SRC_LIST}

        PUBLIC
        ${HEADER_LIST})

## Make files includable
function(make_includable input_file output_file)
    file(READ ${input_file} content)
    set(delim "for_c++_include")
    set(content "R\"${delim}(\n${content})${delim}\"")
    file(WRITE ${output_file} "${content}")
    message(STATUS "Generating file: ${output_file}")
endfunction(make_includable)

make_includable(rest/html/pin.html rest/html/pin.include.html)
make_includable(state/default/config.v4.toml state/default/config.include.toml)

# All users of this library will need at least C++17
target_compile_features(wolf_runner PUBLIC cxx_std_17)

####################
# Executable
add_executable(wolf wolf.cpp)
target_link_libraries(wolf wolf::runner
        #-static # enable to statically link all libraries
)
target_compile_features(wolf PRIVATE cxx_std_17)