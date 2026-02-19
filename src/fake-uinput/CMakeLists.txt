option(BUILD_FAKE_UINPUT "Build fake-uinput library and broker" ON)
if (BUILD_FAKE_UINPUT)
    message(STATUS "Building fake-uinput")

    # Broker daemon (standalone, static)
    add_executable(fake-uinput-broker fake-uinput-broker.cpp)
    target_compile_features(fake-uinput-broker PRIVATE cxx_std_17)
    target_link_libraries(fake-uinput-broker -static)

    # LD_PRELOAD shared library (64-bit)
    add_library(fake_uinput SHARED fake-uinput.cpp)
    add_library(fake-uinput::lib ALIAS fake_uinput)
    target_compile_features(fake_uinput PRIVATE cxx_std_17)
    target_link_libraries(fake_uinput PRIVATE dl)
    set_target_properties(fake_uinput PROPERTIES OUTPUT_NAME "fake-uinput")

    # Optional 32-bit build for Steam's 32-bit reaper
    option(FAKE_UINPUT_32BIT "Build fake-uinput 32-bit library" OFF)
    if (FAKE_UINPUT_32BIT)
        add_library(fake_uinput_32 SHARED fake-uinput.cpp)
        target_compile_features(fake_uinput_32 PRIVATE cxx_std_17)
        target_link_libraries(fake_uinput_32 PRIVATE dl)
        set_target_properties(fake_uinput_32 PROPERTIES
                COMPILE_FLAGS "-m32" LINK_FLAGS "-m32"
                OUTPUT_NAME "fake-uinput-32")
    endif ()
endif ()
