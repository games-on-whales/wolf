# fake-uinput: LD_PRELOAD shim that lets containerized apps (e.g. Steam Input) create uinput
# devices in an unprivileged Wolf session container. It intercepts ioctl(UI_DEV_CREATE/DESTROY)
# and notifies Wolf over the control socket; Wolf performs the privileged mknod + fake-udev.
# Structure mirrors src/fake-udev. Build 64-bit here; the 32-bit variant is built directly in the
# Dockerfile (Steam's client is 32-bit) since the shim has no dependencies beyond libc/libdl.
option(BUILD_FAKE_UINPUT "Build fake-uinput shim library" ON)
if (BUILD_FAKE_UINPUT)
    message(STATUS "Building fake-uinput")

    add_library(fake_uinput SHARED fake-uinput.cpp)
    add_library(fake-uinput::lib ALIAS fake_uinput)

    target_compile_features(fake_uinput PRIVATE cxx_std_17)
    target_link_libraries(fake_uinput PRIVATE ${CMAKE_DL_LIBS})
    set_target_properties(fake_uinput PROPERTIES OUTPUT_NAME "fake-uinput")
    # The 32-bit build is produced directly in docker/wolf.Dockerfile with `g++ -m32` (the shim is a
    # single dependency-free TU) rather than via a multilib CMake toolchain, to keep this build simple.
endif ()
