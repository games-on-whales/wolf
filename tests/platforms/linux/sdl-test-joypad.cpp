#include "catch2/catch_all.hpp"
#include <core/input.hpp>
#include <SDL.h>

using Catch::Matchers::Equals;
using namespace wolf::core::input;

TEST_CASE("SDL Joypads", "[platforms][linux][sdl]") {
  // Create the controller
  auto joypad = Joypad(Joypad::PS, Joypad::RUMBLE | Joypad::ACCELEROMETER | Joypad::GYRO | Joypad::TOUCHPAD);

  SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);

  SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR);
  SDL_JoystickEventState(SDL_ENABLE);
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");

  // Initializing the controller
  SDL_GameController *gc = SDL_GameControllerOpen(0);
  if (gc == nullptr) {
    WARN(SDL_GetError());
  }
  REQUIRE(gc);

  // Checking for basic joypad capabilities
  REQUIRE(SDL_GameControllerHasRumble(gc));
  REQUIRE(SDL_GameControllerHasButton(gc, SDL_CONTROLLER_BUTTON_X));

  // Check if the controller has sensors
  REQUIRE(SDL_GameControllerHasSensor(gc, SDL_SENSOR_GYRO));
  REQUIRE(SDL_GameControllerHasSensor(gc, SDL_SENSOR_ACCEL));

  if (SDL_GameControllerSetSensorEnabled(gc, SDL_SENSOR_ACCEL, SDL_TRUE) != 0) {
    WARN(SDL_GetError());
  };
  if (SDL_GameControllerSetSensorEnabled(gc, SDL_SENSOR_GYRO, SDL_TRUE) != 0) {
    WARN(SDL_GetError());
  }
}