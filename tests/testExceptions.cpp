#include "catch2/catch_all.hpp"
using Catch::Matchers::Contains;
using Catch::Matchers::Equals;

#include <exceptions/exceptions.h>
#include <iostream>

TEST_CASE("Exceptions", "[Exceptions]") {
  safe_dump_stacktrace_to("stacktrace.txt");

  auto trace = load_stacktrace_from("stacktrace.txt");

  auto stacktrace = trace->resolve();
  REQUIRE(stacktrace.frames.size() > 0);
  stacktrace.print(std::cout, false);

  // TODO: seems to be different when running on Github Actions
  //  REQUIRE_THAT(stacktrace.frames[0].filename, EndsWith("src/moonlight-server/exceptions/exceptions.h"));
  //  REQUIRE_THAT(stacktrace.frames[0].symbol, Equals("safe_dump_stacktrace_to"));
}