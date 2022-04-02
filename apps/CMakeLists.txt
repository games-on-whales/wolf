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

# Some libraries don't work with FetchContent_Declare out of the box
# We have to manually git clone them and adding them into `libraries`
target_include_directories(wolf PRIVATE libraries rest state)

target_link_libraries(
        wolf PRIVATE
        ${Boost_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${CMAKE_THREAD_LIBS_INIT}
        fmt::fmt
        moonlight
)

if (UNIX AND NOT APPLE)
    add_compile_definitions(WOLF_PLATFORM="linux")

    find_package(PipeWire REQUIRED)
    target_include_directories(wolf PRIVATE platforms/linux)
    target_link_libraries(wolf PRIVATE PipeWire::PipeWire)
endif ()