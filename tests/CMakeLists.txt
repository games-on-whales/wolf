# Testing library
FetchContent_Declare(
  catch
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v2.13.6)
FetchContent_MakeAvailable(catch)
# Adds Catch2::Catch2

# Tests need to be added as executables first
add_executable(wolflib testlib.cpp)

target_include_directories(wolflib PUBLIC ./)


# I'm using C++17 in the test
target_compile_features(wolflib PRIVATE cxx_std_17)

# Should be linked to the main library, as well as the Catch2 testing library
target_link_libraries(wolflib PRIVATE moonlight Catch2::Catch2)

## Test assets
configure_file(assets/config.json  ${CMAKE_CURRENT_BINARY_DIR}/config.json COPYONLY)
configure_file(assets/server_info_response.xml  ${CMAKE_CURRENT_BINARY_DIR}/server_info_response.xml COPYONLY)

# If you register a test, then ctest and make test will run it.
# You can also run examples and check the output, as well.
add_test(
  NAME wolf_lib_test 
  COMMAND wolflib)
