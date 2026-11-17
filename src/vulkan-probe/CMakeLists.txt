add_library(vulkan-probe)
add_library(wolf::vulkan-probe ALIAS vulkan-probe)

set(SRC_LIST)

find_package(PkgConfig)
pkg_check_modules(GST_VULKAN gstreamer-vulkan-1.0)
if (GST_VULKAN_FOUND)
    message(STATUS "Found GStreamer Vulkan: ${GST_VULKAN_VERSION}")
    pkg_check_modules(GST_VULKAN REQUIRED IMPORTED_TARGET gstreamer-vulkan-1.0)
    list(APPEND SRC_LIST vulkan-probe.cpp)
else ()
    message(STATUS "GStreamer Vulkan not found, building without Vulkan capability probing")
    list(APPEND SRC_LIST no-vulkan-probe.cpp)
endif ()

target_include_directories(vulkan-probe PUBLIC .)
target_sources(vulkan-probe PUBLIC vulkan-probe.hpp PRIVATE ${SRC_LIST})

if (GST_VULKAN_FOUND)
    target_link_libraries(vulkan-probe PUBLIC PkgConfig::GST_VULKAN wolf::core)
else ()
    target_link_libraries(vulkan-probe PUBLIC wolf::core)
endif ()

target_compile_features(vulkan-probe PUBLIC cxx_std_17)
set_target_properties(vulkan-probe PROPERTIES LINKER_LANGUAGE CXX)
