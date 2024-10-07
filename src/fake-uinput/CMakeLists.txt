option(BUILD_FAKE_UINPUT "Build fake-uinput library" ON)
if (BUILD_FAKE_UINPUT)
    message(STATUS "Building fake-uinput")

    add_library(fake_uinput SHARED
            fake-uinput.cpp)
    add_library(fake-uinput::lib ALIAS fake_uinput)

    target_compile_features(fake_uinput PRIVATE cxx_std_17)
    option(FAKE_UINPUT_32BIT "Build fake-uinput 32-bit library" OFF)
    if (FAKE_UINPUT_32BIT)
        set_target_properties(fake_uinput PROPERTIES
                # Force it to 32 bit
                COMPILE_FLAGS "-m32" LINK_FLAGS "-m32"
                # Change the name of the library to indicate it's 32-bit
                OUTPUT_NAME "fake-uinput-32")
    endif ()
endif ()