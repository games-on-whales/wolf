#include "catch2/catch_all.hpp"
#include <core/input.hpp>
#include <helpers/logger.hpp>
#include <SDL.h>

using Catch::Matchers::Equals;
using namespace wolf::core::input;

void flush_sdl_events() {
  SDL_JoystickUpdate();
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    switch (event.type) {
    case SDL_CONTROLLERDEVICEADDED:
      logs::log(logs::info, "SDL_CONTROLLERDEVICEADDED {}", SDL_GameControllerNameForIndex(event.cdevice.which));
      break;
    case SDL_CONTROLLERDEVICEREMOVED:
      logs::log(logs::info, "SDL_CONTROLLERDEVICEREMOVED {}", event.cdevice.which);
      break;
    case SDL_CONTROLLERDEVICEREMAPPED:
      logs::log(logs::info, "SDL_CONTROLLERDEVICEREMAPPED {}", SDL_GameControllerNameForIndex(event.cdevice.which));
      break;
    }
  }
}

class SDLTestsFixture {
public:
  SDLTestsFixture() {
    SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_SENSOR);
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
    SDL_GameControllerEventState(SDL_ENABLE);
  }

  ~SDLTestsFixture() {
    SDL_Quit();
  }
};

#define SDL_TEST_BUTTON(JOYPAD_BTN, SDL_BTN)                                                                           \
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_BTN) == 0);                                                              \
  joypad.set_pressed_buttons(JOYPAD_BTN);                                                                              \
  flush_sdl_events();                                                                                                  \
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_BTN) == 1);

void test_buttons(SDL_GameController *gc, Joypad &joypad) {
  SDL_TEST_BUTTON(Joypad::DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_UP);
  SDL_TEST_BUTTON(Joypad::DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
  SDL_TEST_BUTTON(Joypad::DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
  SDL_TEST_BUTTON(Joypad::DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

  SDL_TEST_BUTTON(Joypad::HOME, SDL_CONTROLLER_BUTTON_GUIDE);
  SDL_TEST_BUTTON(Joypad::START, SDL_CONTROLLER_BUTTON_START);
  SDL_TEST_BUTTON(Joypad::BACK, SDL_CONTROLLER_BUTTON_BACK);

  SDL_TEST_BUTTON(Joypad::LEFT_STICK, SDL_CONTROLLER_BUTTON_LEFTSTICK);
  SDL_TEST_BUTTON(Joypad::RIGHT_STICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
  SDL_TEST_BUTTON(Joypad::LEFT_BUTTON, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
  SDL_TEST_BUTTON(Joypad::RIGHT_BUTTON, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);

  SDL_TEST_BUTTON(Joypad::A, SDL_CONTROLLER_BUTTON_A);
  SDL_TEST_BUTTON(Joypad::B, SDL_CONTROLLER_BUTTON_B);
  SDL_TEST_BUTTON(Joypad::X, SDL_CONTROLLER_BUTTON_X);
  SDL_TEST_BUTTON(Joypad::Y, SDL_CONTROLLER_BUTTON_Y);

  // All together
  joypad.set_pressed_buttons(0);
  flush_sdl_events();
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A) == 0);
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B) == 0);
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X) == 0);
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y) == 0);
  joypad.set_pressed_buttons(Joypad::A | Joypad::B | Joypad::X | Joypad::Y);
  flush_sdl_events();
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A) == 1);
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B) == 1);
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X) == 1);
  REQUIRE(SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y) == 1);
}

void test_rumble(SDL_GameController *gc, Joypad &joypad) {
  // Checking for basic capability
  REQUIRE(SDL_GameControllerHasRumble(gc));

  auto rumble_data = std::make_shared<std::pair<int, int>>();
  joypad.set_on_rumble([rumble_data](int low_freq, int high_freq) {
    rumble_data->first = low_freq;
    rumble_data->second = high_freq;
  });

  // When debugging this, bear in mind that SDL will send max duration here
  // https://github.com/libsdl-org/SDL/blob/da8fc70a83cf6b76d5ea75c39928a7961bd163d3/src/joystick/linux/SDL_sysjoystick.c#L1628
  SDL_GameControllerRumble(gc, 100, 200, 100);
  std::this_thread::sleep_for(30ms); // wait for the effect to be picked up
  REQUIRE(rumble_data->first == 200);
  REQUIRE(rumble_data->second == 100);
}

TEST_CASE_METHOD(SDLTestsFixture, "PS Joypad", "[SDL]") {
  // Create the controller
  auto joypad = Joypad(Joypad::PS, Joypad::RUMBLE | Joypad::ANALOG_TRIGGERS);

  std::this_thread::sleep_for(150ms);

  // Initializing the controller
  flush_sdl_events();
  SDL_GameController *gc = SDL_GameControllerOpen(0);
  if (gc == nullptr) {
    WARN(SDL_GetError());
  }
  REQUIRE(gc);

  REQUIRE(SDL_GameControllerGetType(gc) == SDL_CONTROLLER_TYPE_PS5);

  test_buttons(gc, joypad);
  test_rumble(gc, joypad);

  { // Sticks
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_LEFTX));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

    joypad.set_stick(Joypad::LS, 1000, 2000);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) == 1000);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) == -2000);

    joypad.set_stick(Joypad::RS, 1000, 2000);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) == 1000);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) == -2000);

    joypad.set_triggers(10, 20);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 1284);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 2569);

    joypad.set_triggers(0, 0);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);
  }

  // TODO: fixme
  //  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
  //  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
  //  { // Additional sensors
  //    // Check if the controller has sensors
  //    REQUIRE(SDL_GameControllerHasSensor(gc, SDL_SENSOR_GYRO));
  //    REQUIRE(SDL_GameControllerHasSensor(gc, SDL_SENSOR_ACCEL));
  //    if (SDL_GameControllerSetSensorEnabled(gc, SDL_SENSOR_ACCEL, SDL_TRUE) != 0) {
  //      WARN(SDL_GetError());
  //    };
  //    if (SDL_GameControllerSetSensorEnabled(gc, SDL_SENSOR_GYRO, SDL_TRUE) != 0) {
  //      WARN(SDL_GetError());
  //    }
  //  }
  SDL_GameControllerClose(gc);
}

TEST_CASE_METHOD(SDLTestsFixture, "XBOX Joypad", "[SDL]") {
  // Create the controller
  auto joypad = Joypad(Joypad::XBOX, Joypad::RUMBLE | Joypad::ANALOG_TRIGGERS);

  std::this_thread::sleep_for(150ms);

  // Initializing the controller
  flush_sdl_events();
  SDL_GameController *gc = SDL_GameControllerOpen(0);
  if (gc == nullptr) {
    WARN(SDL_GetError());
  }
  REQUIRE(gc);
  REQUIRE(SDL_GameControllerGetType(gc) == SDL_CONTROLLER_TYPE_XBOXONE);
  // Checking for basic joypad capabilities
  REQUIRE(SDL_GameControllerHasRumble(gc));

  test_buttons(gc, joypad);
  test_rumble(gc, joypad);

  { // Sticks
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_LEFTX));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

    joypad.set_stick(Joypad::LS, 1000, 2000);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) == 1000);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) == -2000);

    joypad.set_stick(Joypad::RS, 1000, 2000);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) == 1000);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) == -2000);

    joypad.set_triggers(10, 20);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 1284);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 2569);

    joypad.set_triggers(0, 0);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);
  }

  SDL_GameControllerClose(gc);
}

TEST_CASE_METHOD(SDLTestsFixture, "Nintendo Joypad", "[SDL]") {
  // Create the controller
  auto joypad = Joypad(Joypad::NINTENDO, Joypad::RUMBLE);

  std::this_thread::sleep_for(150ms);

  // Initializing the controller
  flush_sdl_events();
  SDL_GameController *gc = SDL_GameControllerOpen(0);
  if (gc == nullptr) {
    WARN(SDL_GetError());
  }
  REQUIRE(gc);
  REQUIRE(SDL_GameControllerGetType(gc) == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO);

  test_buttons(gc, joypad);
  test_rumble(gc, joypad);

  { // Sticks
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_LEFTX));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    REQUIRE(SDL_GameControllerHasAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

    joypad.set_stick(Joypad::LS, 1000, 2000);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) == 1000);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) == -2000);

    joypad.set_stick(Joypad::RS, 1000, 2000);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) == 1000);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) == -2000);

    // Nintendo ONLY: triggers are buttons, so it can only be MAX or 0
    joypad.set_triggers(10, 20);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 32767);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 32767);

    joypad.set_triggers(0, 0);
    flush_sdl_events();
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0);
    REQUIRE(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);
  }

  SDL_GameControllerClose(gc);
}