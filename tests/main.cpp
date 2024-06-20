#define CATCH_CONFIG_FAST_COMPILE

#include <catch2/catch_session.hpp>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>

int main(int argc, char *argv[]) {
  logs::init(logs::parse_level(utils::get_env("WOLF_LOG_LEVEL", "TRACE")));

  int result = Catch::Session().run(argc, argv);

  return result;
}