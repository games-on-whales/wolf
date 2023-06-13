# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_docker)
add_library(wolf::docker ALIAS wolf_docker)

target_include_directories(wolf_docker PUBLIC .)
target_sources(wolf_docker
        PUBLIC
        docker/docker.hpp
        docker/formatters.hpp

        PRIVATE
        docker/json_formatters.hpp
        docker/docker.cpp)

FetchContent_Declare(
        boost_json
        GIT_REPOSITORY https://github.com/boostorg/json.git
        GIT_TAG "boost-1.75.0")

FetchContent_GetProperties(boost_json)
if (NOT boost_json_POPULATED)
    FetchContent_Populate(boost_json)

    add_library(boost_json INTERFACE)
    target_include_directories(boost_json INTERFACE ${boost_json_SOURCE_DIR}/include)
    target_link_libraries_system(wolf_docker PRIVATE boost_json)
endif ()

find_package(CURL) # REQUIRED UnixSockets removed because it doesn't work in Github Actions
target_link_libraries_system(wolf_docker
        PRIVATE
        CURL::libcurl
        wolf::helpers)

# All users of this library will need at least C++17
target_compile_features(wolf_docker PUBLIC cxx_std_17)