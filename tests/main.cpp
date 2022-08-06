#include "catch2/catch_all.hpp"
#include "catch2/matchers/catch_matchers_all.hpp"
using Catch::Matchers::Equals;
#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file

#include "testCrypto.cpp"
#include "testMoonlight.cpp"
#include "testRTSP.cpp"