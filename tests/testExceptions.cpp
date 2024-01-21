#include "catch2/catch_all.hpp"
using Catch::Matchers::EndsWith;
using Catch::Matchers::Equals;

#include <exceptions/exceptions.h>
#include <fstream>

TEST_CASE("Exceptions", "[Exceptions]") {
  std::ofstream ofs("stacktrace.txt");
  safe_dump_stacktrace_to(ofs);
  ofs.close();

  std::ifstream ifs("stacktrace.txt");
  auto trace = load_stacktrace_from(ifs);
  REQUIRE(trace->frames.size() > 0);

  auto stacktrace = trace->resolve();
  REQUIRE(stacktrace.frames.size() > 0);
  REQUIRE_THAT(stacktrace.frames[0].filename, EndsWith("src/moonlight-server/exceptions/exceptions.h"));
  REQUIRE_THAT(stacktrace.frames[0].symbol, Equals("safe_dump_stacktrace_to"));
}