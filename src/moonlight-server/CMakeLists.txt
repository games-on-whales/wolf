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
        GIT_TAG 11.1.4)
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
        GIT_TAG v3.4.0
        OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(tomlplusplus)

# A nice little trick to disable warnings for external dependencies
# see: https://stackoverflow.com/a/76128930
get_directory_property(old_dir_compile_options COMPILE_OPTIONS)
add_compile_options(-w)

# Serialization
FetchContent_Declare(
        reflect-cpp
        GIT_REPOSITORY https://github.com/getml/reflect-cpp
        GIT_TAG v0.21.0
)
set(REFLECTCPP_USE_VCPKG OFF)

set(REFLECTCPP_BSON OFF)
set(REFLECTCPP_CBOR OFF)
set(REFLECTCPP_FLEXBUFFERS OFF)
set(REFLECTCPP_MSGPACK OFF)
set(REFLECTCPP_TOML ON)
set(REFLECTCPP_XML OFF)
set(REFLECTCPP_YAML OFF)

FetchContent_MakeAvailable(reflect-cpp)
target_link_libraries(wolf_runner PUBLIC reflectcpp)
# Restore warnings
set_directory_properties(PROPERTIES COMPILE_OPTIONS "${old_dir_compile_options}")


# HTTP server library
FetchContent_Declare(
        simplewebserver
        # Forked from https://gitlab.com/eidheim/Simple-Web-Server in order to support latest boost
        # see: https://gitlab.com/eidheim/Simple-Web-Server/-/merge_requests/283
        GIT_REPOSITORY https://gitlab.com/ReenigneArcher/Simple-Web-Server.git
        GIT_TAG 546895a9)
FetchContent_MakeAvailable(simplewebserver)
target_link_libraries_system(wolf_runner PUBLIC simple-web-server)

# mDNS
FetchContent_Declare(
        mdns_cpp
        GIT_REPOSITORY https://github.com/games-on-whales/mdns_cpp.git)
FetchContent_MakeAvailable(mdns_cpp)
target_link_libraries_system(wolf_runner PUBLIC mdns_cpp::mdns_cpp)


# Enet
set(CMAKE_PROJECT_INCLUDE_BEFORE "${PROJECT_SOURCE_DIR}/cmake/EnableCMP0048.cmake")
FetchContent_Declare(
        enet
        GIT_REPOSITORY https://github.com/cgutman/enet
        GIT_TAG 44c85e16279553d9c052e572bcbfcd745fb74abf)
FetchContent_MakeAvailable(enet)
target_link_libraries_system(wolf_runner PUBLIC enet)
unset(CMAKE_PROJECT_INCLUDE_BEFORE)

# Stacktrace
FetchContent_Declare(
        cpptrace
        GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
        GIT_TAG v0.8.2
)

pkg_check_modules(LIBUNWIND QUIET libunwind)
if (LIBUNWIND_FOUND)
    set(CPPTRACE_UNWIND_WITH_LIBUNWIND ON)
else ()
    message(WARNING "Missing libunwind, stacktraces will not be available.")
endif ()
set(ZSTD_BUILD_SHARED ${BUILD_SHARED_LIBS})
set(ZSTD_BUILD_STATIC OFF)

if (NOT BUILD_SHARED_LIBS)
    set(ZSTD_BUILD_STATIC ON)
endif ()
FetchContent_MakeAvailable(cpptrace)

##############################

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})

target_link_libraries(
        wolf_runner PUBLIC
        wolf::moonlight
        wolf::helpers
        wolf::core
        gst-video::context
        cpptrace::cpptrace
        ${Boost_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${CMAKE_THREAD_LIBS_INIT})

target_include_directories(wolf_runner PUBLIC .)

file(GLOB HEADER_LIST CONFIGURE_DEPENDS */*.hpp)
file(GLOB SRC_LIST SRCS
        api/*.cpp
        audio/*.cpp
        control/*.cpp
        gst-plugin/*.cpp
        rest/*.cpp
        rtp/*.cpp
        rtsp/*.cpp
        runners/*.cpp
        sessions/*.cpp
        state/*.cpp
        streaming/*.cpp
        introspect/*.cpp)

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
make_includable(state/default/config.v6.toml state/default/config.include.toml)

# All users of this library will need at least C++17
target_compile_features(wolf_runner PUBLIC cxx_std_17)

####################
# Executable
add_executable(wolf wolf.cpp)
target_link_libraries(wolf wolf::runner
        #-static # enable to statically link all libraries
)
target_compile_features(wolf PRIVATE cxx_std_17)
