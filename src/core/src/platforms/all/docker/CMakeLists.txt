# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_docker)
add_library(wolf::docker ALIAS wolf_docker)

target_include_directories(wolf_docker PUBLIC . PRIVATE ../../../)
target_sources(wolf_docker
        PUBLIC
        docker/formatters.hpp

        PRIVATE
        docker/json_formatters.hpp
        docker/docker.cpp)

find_package(CURL) # REQUIRED UnixSockets removed because it doesn't work in Github Actions
target_link_libraries_system(wolf_docker
        PRIVATE
        CURL::libcurl
        wolf::helpers)

# All users of this library will need at least C++17
target_compile_features(wolf_docker PUBLIC cxx_std_17)