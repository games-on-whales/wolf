# Make an automatic library - will be static or dynamic based on user setting
add_library(wolf_audio)
add_library(wolf::audio ALIAS wolf_audio)

target_include_directories(wolf_audio PRIVATE ../../../)
find_package(Boost 1.77 REQUIRED)
target_link_libraries_system(wolf_audio
        PUBLIC
        Boost::boost
        wolf::helpers)


find_package(PkgConfig)
find_package(PulseAudio REQUIRED)
if (NOT (PULSEAUDIO_FOUND))
    message(FATAL_ERROR "Please install libpulse: CMake will Exit")
endif ()
message(STATUS "Found PulseAudio: ${PULSEAUDIO_VERSION}")
target_link_libraries(wolf_audio PUBLIC pulse)
target_include_directories(wolf_audio PUBLIC ${PULSEAUDIO_INCLUDE_DIR})
target_sources(wolf_audio PUBLIC pulse.cpp)

# All users of this library will need at least C++17
target_compile_features(wolf_audio PUBLIC cxx_std_17)