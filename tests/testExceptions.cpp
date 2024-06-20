#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

using Catch::Matchers::Contains;
using Catch::Matchers::Equals;

#include <exceptions/exceptions.h>
#include <iostream>

TEST_CASE("Exceptions", "[Exceptions]") {
  safe_dump_stacktrace_to("stacktrace.txt");
  auto stacktrace = load_stacktrace_from("stacktrace.txt")->resolve();

  REQUIRE(stacktrace.frames.size() > 0);
  stacktrace.print(std::cout, false);

  // TODO: seems to be different when running on Github Actions
  //  REQUIRE_THAT(stacktrace.frames[0].filename, EndsWith("src/moonlight-server/exceptions/exceptions.h"));
  //  REQUIRE_THAT(stacktrace.frames[0].symbol, Equals("safe_dump_stacktrace_to"));
}