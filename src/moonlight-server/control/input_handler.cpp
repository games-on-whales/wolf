#include <boost/endian/conversion.hpp>
#include <boost/locale.hpp>
#include <control/input_handler.hpp>
#include <events/events.hpp>
#include <helpers/logger.hpp>
#include <immer/box.hpp>
#include <platforms/input.hpp>
#include <string>

namespace control {

using namespace wolf::core::virtual_display;
using namespace wolf::core::input;
using namespace wolf::core;
using namespace std::string_literals;
using namespace moonlight::control;

std::shared_ptr<events::JoypadTypes> create_new_joypad(const events::StreamSession &session,
                                                       immer::box<std::shared_ptr<ENetPeer>> connected_client,
                                                       int controller_number,
                                                       CONTROLLER_TYPE requested_type,
                                                       uint8_t capabilities) {

  auto on_rumble_fn = ([connected_client, controller_number, aes_key = session.aes_key](int low_freq, int high_freq) {
    auto rumble_pkt = ControlRumblePacket{
        .header = {.type = RUMBLE_DATA, .length = sizeof(ControlRumblePacket) - sizeof(ControlPacket)},
        .controller_number = boost::endian::native_to_little((uint16_t)controller_number),
        .low_freq = boost::endian::native_to_little((uint16_t)low_freq),
        .high_freq = boost::endian::native_to_little((uint16_t)high_freq)};
    std::string plaintext = {(char *)&rumble_pkt, sizeof(rumble_pkt)};
    encrypt_and_send(plaintext, aes_key, connected_client);
  });

  auto on_led_fn = ([connected_client, controller_number, aes_key = session.aes_key](int r, int g, int b) {
    auto led_pkt = ControlRGBLedPacket{
        .header{.type = RGB_LED_EVENT, .length = sizeof(ControlRGBLedPacket) - sizeof(ControlPacket)},
        .controller_number = boost::endian::native_to_little((uint16_t)controller_number),
        .r = static_cast<uint8_t>(r),
        .g = static_cast<uint8_t>(g),
        .b = static_cast<uint8_t>(b)};
    std::string plaintext = {(char *)&led_pkt, sizeof(led_pkt)};
    encrypt_and_send(plaintext, aes_key, connected_client);
  });

  auto on_adaptive_trigger_fn = ([connected_client, controller_number, aes_key = session.aes_key](
                                     const inputtino::PS5Joypad::TriggerEffect &effect) {
    auto rumble_pkt = ControlAdaptiveTriggerPacket{
        .header{.type = ADAPTIVE_TRIGGER_EVENT, .length = sizeof(ControlAdaptiveTriggerPacket) - sizeof(ControlPacket)},
        .controller_number = boost::endian::native_to_little((uint16_t)controller_number),
        .effect = effect};
    std::string plaintext = {(char *)&rumble_pkt, sizeof(rumble_pkt)};
    encrypt_and_send(plaintext, aes_key, connected_client);
  });

  std::shared_ptr<events::JoypadTypes> new_pad;
  auto controllers_override = session.client_settings->controllers_override;
  auto final_type = controllers_override.size() > controller_number ? controllers_override[controller_number]
                                                                    : wolf::config::ControllerType::AUTO;
  if (final_type == wolf::config::ControllerType::AUTO) {
    switch (requested_type) {
    case XBOX:
      final_type = wolf::config::ControllerType::XBOX;
      break;
    case PS:
      final_type = wolf::config::ControllerType::PS;
      break;
    case NINTENDO:
      final_type = wolf::config::ControllerType::NINTENDO;
      break;
    default:
      final_type = wolf::config::ControllerType::AUTO;
      break;
    }
  }
  switch (final_type) {
  case wolf::config::ControllerType::AUTO:
  case wolf::config::ControllerType::XBOX: {
    logs::log(logs::info, "Creating Xbox joypad for controller {}", controller_number);
    auto result =
        XboxOneJoypad::create({.name = "Wolf X-Box One (virtual) pad",
                               // https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c#L147
                               .vendor_id = 0x045E,
                               .product_id = 0x02EA,
                               .version = 0x0408});
    if (!result) {
      logs::log(logs::error, "Failed to create Xbox One joypad: {}", result.getErrorMessage());
      return {};
    } else {
      (*result).set_on_rumble(on_rumble_fn);
      new_pad = std::make_shared<events::JoypadTypes>(std::move(*result));
    }
    break;
  }
  case wolf::config::ControllerType::PS: {
    logs::log(logs::info, "Creating PS joypad for controller {}", controller_number);
    auto result = PS5Joypad::create(
        {.name = "Wolf DualSense (virtual) pad", .vendor_id = 0x054C, .product_id = 0x0CE6, .version = 0x8111});
    if (!result) {
      logs::log(logs::error, "Failed to create PS5 joypad: {}", result.getErrorMessage());
      return {};
    } else {
      (*result).set_on_rumble(on_rumble_fn);
      (*result).set_on_led(on_led_fn);
      (*result).set_on_trigger_effect(on_adaptive_trigger_fn);
      new_pad = std::make_shared<events::JoypadTypes>(std::move(*result));

      // Let's wait for the kernel to pick it up and mount the /dev/ devices
      std::this_thread::sleep_for(std::chrono::milliseconds(50));

      std::visit(
          [&session](auto &pad) {
            if (auto wl = *session.wayland_display->load()) {
              for (const auto node : pad.get_udev_events()) {
                if (node.find("ID_INPUT_TOUCHPAD") != node.end()) {
                  add_input_device(*wl, node.at("DEVNAME"));
                }
              }
            }
          },
          *new_pad);
    }
    break;
  }
  case wolf::config::ControllerType::NINTENDO:
    logs::log(logs::info, "Creating Nintendo joypad for controller {}", controller_number);
    auto result = SwitchJoypad::create({.name = "Wolf Nintendo (virtual) pad",
                                        // https://github.com/torvalds/linux/blob/master/drivers/hid/hid-ids.h#L981
                                        .vendor_id = 0x057e,
                                        .product_id = 0x2009,
                                        .version = 0x8111});
    if (!result) {
      logs::log(logs::error, "Failed to create Switch joypad: {}", result.getErrorMessage());
      return {};
    } else {
      (*result).set_on_rumble(on_rumble_fn);
      new_pad = std::make_shared<events::JoypadTypes>(std::move(*result));
    }
    break;
  }

  if (capabilities & ACCELEROMETER && final_type == wolf::config::ControllerType::PS) {
    // Request acceleromenter events from the client at 100 Hz
    logs::log(logs::info, "Requesting accelerometer events for controller {}", controller_number);
    auto accelerometer_pkt = ControlMotionEventPacket{
        .header{.type = MOTION_EVENT, .length = sizeof(ControlMotionEventPacket) - sizeof(ControlPacket)},
        .controller_number = static_cast<uint16_t>(controller_number),
        .reportrate = 100,
        .type = ACCELERATION};
    std::string plaintext = {(char *)&accelerometer_pkt, sizeof(accelerometer_pkt)};
    encrypt_and_send(plaintext, session.aes_key, connected_client);
  }

  if (capabilities & GYRO && final_type == wolf::config::ControllerType::PS) {
    // Request gyroscope events from the client at 100 Hz
    logs::log(logs::info, "Requesting gyroscope events for controller {}", controller_number);
    auto gyro_pkt = ControlMotionEventPacket{
        .header{.type = MOTION_EVENT, .length = sizeof(ControlMotionEventPacket) - sizeof(ControlPacket)},
        .controller_number = static_cast<uint16_t>(controller_number),
        .reportrate = 100,
        .type = GYROSCOPE};
    std::string plaintext = {(char *)&gyro_pkt, sizeof(gyro_pkt)};
    encrypt_and_send(plaintext, session.aes_key, connected_client);
  }

  session.joypads->update([&](events::JoypadList joypads) {
    logs::log(logs::debug,
              "[INPUT] Sending PlugDeviceEvent for joypad {} of type: {}",
              controller_number,
              (int)final_type);

    events::PlugDeviceEvent unplug_ev{.session_id = session.session_id};
    std::visit(
        [&unplug_ev](auto &pad) {
          unplug_ev.udev_events = pad.get_udev_events();
          unplug_ev.udev_hw_db_entries = pad.get_udev_hw_db_entries();
        },
        *new_pad);
    session.event_bus->fire_event(immer::box<events::PlugDeviceEvent>(unplug_ev));
    return joypads.set(controller_number, new_pad);
  });
  return new_pad;
}

/**
 * Creates a new PenTablet and saves it into the session;
 * will also trigger a PlugDeviceEvent
 */
bool create_pen_tablet(events::StreamSession &session) {
  logs::log(logs::debug, "[INPUT] Creating new pen tablet");
  auto tablet = PenTablet::create();
  if (!tablet) {
    logs::log(logs::error, "Failed to create pen tablet: {}", tablet.getErrorMessage());
    return false;
  }
  auto tablet_ptr = std::make_shared<PenTablet>(std::move(*tablet));
  session.event_bus->fire_event(immer::box<events::PlugDeviceEvent>(
      events::PlugDeviceEvent{.session_id = session.session_id,
                              .udev_events = tablet_ptr->get_udev_events(),
                              .udev_hw_db_entries = tablet_ptr->get_udev_hw_db_entries()}));
  if (auto wl = *session.wayland_display->load()) {
    for (const auto node : tablet_ptr->get_nodes()) {
      add_input_device(*wl, node);
    }
  }
  session.pen_tablet->emplace(std::move(*tablet_ptr));
  return true;
}

/**
 * Creates a new Touch screen and saves it into the session;
 * will also trigger a PlugDeviceEvent
 */
bool create_touch_screen(events::StreamSession &session) {
  logs::log(logs::debug, "[INPUT] Creating new touch screen");
  auto touch = TouchScreen::create();
  if (!touch) {
    logs::log(logs::error, "Failed to create touch screen: {}", touch.getErrorMessage());
    return false;
  }
  auto touch_screen = std::make_shared<TouchScreen>(std::move(*touch));
  session.event_bus->fire_event(immer::box<events::PlugDeviceEvent>(
      events::PlugDeviceEvent{.session_id = session.session_id,
                              .udev_events = touch_screen->get_udev_events(),
                              .udev_hw_db_entries = touch_screen->get_udev_hw_db_entries()}));
  if (auto wl = *session.wayland_display->load()) {
    for (const auto node : touch_screen->get_nodes()) {
      add_input_device(*wl, node);
    }
  }
  session.touch_screen->emplace(std::move(*touch_screen));
  return true;
}

float netfloat_to_0_1(const utils::netfloat &f) {
  return std::clamp(utils::from_netfloat(f), 0.0f, 1.0f);
}

static inline float deg2rad(float degree) {
  return degree * (M_PI / 180.f);
}

void mouse_move_rel(const MOUSE_MOVE_REL_PACKET &pkt, events::StreamSession &session) {
  if (session.mouse->has_value()) {
    auto pointer_acceleration = session.client_settings->mouse_acceleration;
    auto delta_x = static_cast<float>(boost::endian::big_to_native(pkt.delta_x)) * pointer_acceleration;
    auto delta_y = static_cast<float>(boost::endian::big_to_native(pkt.delta_y)) * pointer_acceleration;
    std::visit([delta_x, delta_y](auto &mouse) { mouse.move(delta_x, delta_y); }, session.mouse->value());
  } else {
    logs::log(logs::warning, "Received MOUSE_MOVE_REL_PACKET but no mouse device is present");
  }
}

void mouse_move_abs(const MOUSE_MOVE_ABS_PACKET &pkt, events::StreamSession &session) {
  if (session.mouse->has_value()) {
    auto pointer_acceleration = session.client_settings->mouse_acceleration;
    float x = boost::endian::big_to_native(pkt.x);
    float y = boost::endian::big_to_native(pkt.y);
    float window_width = boost::endian::big_to_native(pkt.width);
    float window_height = boost::endian::big_to_native(pkt.height);

    auto absolute_x = (x / window_width) * static_cast<float>(session.display_mode.width) * pointer_acceleration;
    auto absolute_y = (y / window_height) * static_cast<float>(session.display_mode.height) * pointer_acceleration;

    std::visit([absolute_x,
                absolute_y,
                screen_width = session.display_mode.width,
                screen_height = session.display_mode.height](
                   auto &mouse) { mouse.move_abs(absolute_x, absolute_y, screen_width, screen_height); },
               session.mouse->value());
  } else {
    logs::log(logs::warning, "Received MOUSE_MOVE_ABS_PACKET but no mouse device is present");
  }
}

void mouse_button(const MOUSE_BUTTON_PACKET &pkt, events::StreamSession &session) {
  if (session.mouse->has_value()) {
    if (std::holds_alternative<state::input::Mouse>(session.mouse->value())) {
      Mouse::MOUSE_BUTTON btn_type;

      switch (pkt.button) {
      case 1:
        btn_type = Mouse::LEFT;
        break;
      case 2:
        btn_type = Mouse::MIDDLE;
        break;
      case 3:
        btn_type = Mouse::RIGHT;
        break;
      case 4:
        btn_type = Mouse::SIDE;
        break;
      default:
        btn_type = Mouse::EXTRA;
        break;
      }
      if (pkt.type == MOUSE_BUTTON_PRESS) {
        std::get<state::input::Mouse>(session.mouse->value()).press(btn_type);
      } else {
        std::get<state::input::Mouse>(session.mouse->value()).release(btn_type);
      }
    } else if (std::holds_alternative<wolf::core::virtual_display::WaylandMouse>(session.mouse->value())) {
      if (pkt.type == MOUSE_BUTTON_PRESS) {
        std::get<wolf::core::virtual_display::WaylandMouse>(session.mouse->value()).press(pkt.button);
      } else {
        std::get<wolf::core::virtual_display::WaylandMouse>(session.mouse->value()).release(pkt.button);
      }
    }
  } else {
    logs::log(logs::warning, "Received MOUSE_BUTTON_PACKET but no mouse device is present");
  }
}

void mouse_scroll(const MOUSE_SCROLL_PACKET &pkt, events::StreamSession &session) {
  if (session.mouse->has_value()) {
    std::visit(
        [session, scroll_amount = boost::endian::big_to_native(pkt.scroll_amt1)](auto &mouse) {
          auto scroll_acceleration = session.client_settings->v_scroll_acceleration;
          mouse.vertical_scroll(scroll_amount * scroll_acceleration);
        },
        session.mouse->value());
  } else {
    logs::log(logs::warning, "Received MOUSE_SCROLL_PACKET but no mouse device is present");
  }
}

void mouse_h_scroll(const MOUSE_HSCROLL_PACKET &pkt, events::StreamSession &session) {
  if (session.mouse->has_value()) {
    std::visit(
        [session, scroll_amount = boost::endian::big_to_native(pkt.scroll_amount)](auto &mouse) {
          auto scroll_acceleration = session.client_settings->h_scroll_acceleration;
          mouse.horizontal_scroll(scroll_amount * scroll_acceleration);
        },
        session.mouse->value());
  } else {
    logs::log(logs::warning, "Received MOUSE_HSCROLL_PACKET but no mouse device is present");
  }
}

void keyboard_key(const KEYBOARD_PACKET &pkt, events::StreamSession &session) {
  // moonlight always sets the high bit; not sure why but mask it off here
  short moonlight_key = (short)boost::endian::little_to_native(pkt.key_code) & (short)0x7fff;
  if (session.keyboard->has_value()) {
    if (pkt.type == KEY_PRESS) {
      // Press the virtual modifiers
      if (pkt.modifiers & KEYBOARD_MODIFIERS::SHIFT && moonlight_key != M_SHIFT)
        std::visit([](auto &keyboard) { keyboard.press(M_SHIFT); }, session.keyboard->value());
      if (pkt.modifiers & KEYBOARD_MODIFIERS::CTRL && moonlight_key != M_CTRL)
        std::visit([](auto &keyboard) { keyboard.press(M_CTRL); }, session.keyboard->value());
      if (pkt.modifiers & KEYBOARD_MODIFIERS::ALT && moonlight_key != M_ALT)
        std::visit([](auto &keyboard) { keyboard.press(M_ALT); }, session.keyboard->value());
      if (pkt.modifiers & KEYBOARD_MODIFIERS::META && moonlight_key != M_META)
        std::visit([](auto &keyboard) { keyboard.press(M_META); }, session.keyboard->value());

      // Press the actual key
      std::visit([moonlight_key](auto &keyboard) { keyboard.press(moonlight_key); }, session.keyboard->value());

      // Release the virtual modifiers
      if (pkt.modifiers & KEYBOARD_MODIFIERS::SHIFT && moonlight_key != M_SHIFT)
        std::visit([](auto &keyboard) { keyboard.release(M_SHIFT); }, session.keyboard->value());
      if (pkt.modifiers & KEYBOARD_MODIFIERS::CTRL && moonlight_key != M_CTRL)
        std::visit([](auto &keyboard) { keyboard.release(M_CTRL); }, session.keyboard->value());
      if (pkt.modifiers & KEYBOARD_MODIFIERS::ALT && moonlight_key != M_ALT)
        std::visit([](auto &keyboard) { keyboard.release(M_ALT); }, session.keyboard->value());
      if (pkt.modifiers & KEYBOARD_MODIFIERS::META && moonlight_key != M_META)
        std::visit([](auto &keyboard) { keyboard.release(M_META); }, session.keyboard->value());

    } else {
      std::visit([moonlight_key](auto &keyboard) { keyboard.release(moonlight_key); }, session.keyboard->value());
    }
  } else {
    logs::log(logs::warning, "Received KEYBOARD_PACKET but no keyboard device is present");
  }
}

void utf8_text(const UTF8_TEXT_PACKET &pkt, events::StreamSession &session) {
  if (session.keyboard->has_value()) {
    /* Here we receive a single UTF-8 encoded char at a time,
     * the trick is to convert it to UTF-32 then send CTRL+SHIFT+U+<HEXCODE> in order to produce any
     * unicode character, see: https://en.wikipedia.org/wiki/Unicode_input
     *
     * ex:
     * - when receiving UTF-8 [0xF0 0x9F 0x92 0xA9] (which is '💩')
     * - we'll convert it to UTF-32 [0x1F4A9]
     * - then type: CTRL+SHIFT+U+1F4A9
     * see the conversion at: https://www.compart.com/en/unicode/U+1F4A9
     */
    auto size = boost::endian::big_to_native(pkt.data_size) - sizeof(pkt.packet_type) - 2;
    /* Reading input text as UTF-8 */
    auto utf8 = boost::locale::conv::to_utf<wchar_t>(pkt.text, pkt.text + size, "UTF-8");
    /* Converting to UTF-32 */
    auto utf32 = boost::locale::conv::utf_to_utf<char32_t>(utf8);
    wolf::platforms::input::paste_utf(session.keyboard->value(), utf32);
  } else {
    logs::log(logs::warning, "Received UTF8_TEXT_PACKET but no keyboard device is present");
  }
}

void touch(const TOUCH_PACKET &pkt, events::StreamSession &session) {
  bool has_touch_device = session.touch_screen->has_value();
  if (!has_touch_device) {
    has_touch_device = create_touch_screen(session);
  }
  if (has_touch_device) {
    auto finger_id = boost::endian::little_to_native(pkt.pointer_id);
    auto x = netfloat_to_0_1(pkt.x);
    auto y = netfloat_to_0_1(pkt.y);
    auto pressure_or_distance = netfloat_to_0_1(pkt.pressure_or_distance);
    std::visit(
        [&pkt, &session, &x, &y, &finger_id, &pressure_or_distance](auto &screen) {
          using T = std::decay_t<decltype(screen)>;
          if constexpr (std::is_same_v<T, input::TouchScreen>) {
            // Inputtino TouchScreen only defines place_finger and release_finger methods
            switch (pkt.event_type) {
            case pkts::TOUCH_EVENT_HOVER:
            case pkts::TOUCH_EVENT_DOWN:
            case pkts::TOUCH_EVENT_MOVE: {
              // Convert our 0..360 range to -90..90 relative to Y axis
              int adjusted_angle = pkt.rotation;

              if (adjusted_angle > 90 && adjusted_angle < 270) {
                // Lower hemisphere
                adjusted_angle = 180 - adjusted_angle;
              }

              // Wrap the value if it's out of range
              if (adjusted_angle > 90) {
                adjusted_angle -= 360;
              } else if (adjusted_angle < -90) {
                adjusted_angle += 360;
              }
              screen.place_finger(finger_id, x, y, pressure_or_distance, adjusted_angle);
              break;
            }
            case pkts::TOUCH_EVENT_UP:
            case pkts::TOUCH_EVENT_HOVER_LEAVE:
            case pkts::TOUCH_EVENT_CANCEL:
              screen.release_finger(finger_id);
              break;
            default:
              logs::log(logs::warning, "[INPUT] Unknown touch event type {}", (int)pkt.event_type);
            }
          } else if constexpr (std::is_same_v<T, virtual_display::WaylandTouchScreen>) {
            // For WaylandTouchScreen, we handle the events differently
            switch (pkt.event_type) {
            case pkts::TOUCH_EVENT_HOVER:
            case pkts::TOUCH_EVENT_DOWN:
              screen.down(finger_id, x, y);
              break;
            case pkts::TOUCH_EVENT_MOVE:
              screen.motion(finger_id, x, y);
              break;
            case pkts::TOUCH_EVENT_UP:
              screen.up(finger_id);
              break;
            case pkts::TOUCH_EVENT_HOVER_LEAVE:
            case pkts::TOUCH_EVENT_CANCEL:
              screen.cancel();
              break;
            default:
              logs::log(logs::warning, "[INPUT] Unknown touch event type {}", (int)pkt.event_type);
            }
            // Moonlight TOUCH_EVENT does not include TouchFrame events, so we trigger it manually every time
            screen.frame();
          }
        },
        **session.touch_screen);
  }
}

void pen(const PEN_PACKET &pkt, events::StreamSession &session) {
  bool has_pen_device = session.pen_tablet->has_value();
  if (!has_pen_device) {
    create_pen_tablet(session);
  }

  if (has_pen_device) {
    // First set the buttons
    session.pen_tablet->value().set_btn(PenTablet::PRIMARY, pkt.pen_buttons & PEN_BUTTON_TYPE_PRIMARY);
    session.pen_tablet->value().set_btn(PenTablet::SECONDARY, pkt.pen_buttons & PEN_BUTTON_TYPE_SECONDARY);
    session.pen_tablet->value().set_btn(PenTablet::TERTIARY, pkt.pen_buttons & PEN_BUTTON_TYPE_TERTIARY);

    // Set the tool
    PenTablet::TOOL_TYPE tool;
    switch (pkt.tool_type) {
    case moonlight::control::pkts::TOOL_TYPE_PEN:
      tool = PenTablet::PEN;
      break;
    case moonlight::control::pkts::TOOL_TYPE_ERASER:
      tool = PenTablet::ERASER;
      break;
    default:
      tool = PenTablet::SAME_AS_BEFORE;
      break;
    }

    auto pressure_or_distance = netfloat_to_0_1(pkt.pressure_or_distance);

    // Normalize rotation value to 0-359 degree range
    auto rotation = boost::endian::little_to_native(pkt.rotation);
    if (rotation != PEN_ROTATION_UNKNOWN) {
      rotation %= 360;
    }

    // Here we receive:
    //  - Rotation: degrees from vertical in Y dimension (parallel to screen, 0..360)
    //  - Tilt: degrees from vertical in Z dimension (perpendicular to screen, 0..90)
    float tilt_x = 0;
    float tilt_y = 0;
    // Convert polar coordinates into Y tilt angles
    if (pkt.tilt != PEN_TILT_UNKNOWN && rotation != PEN_ROTATION_UNKNOWN) {
      auto rotation_rads = deg2rad(rotation);
      auto tilt_rads = deg2rad(pkt.tilt);
      auto r = std::sin(tilt_rads);
      auto z = std::cos(tilt_rads);

      tilt_x = std::atan2(std::sin(-rotation_rads) * r, z) * 180.f / M_PI;
      tilt_y = std::atan2(std::cos(-rotation_rads) * r, z) * 180.f / M_PI;
    }

    bool is_touching = pkt.event_type == TOUCH_EVENT_DOWN || pkt.event_type == TOUCH_EVENT_MOVE;

    session.pen_tablet->value().place_tool(tool,
                                           netfloat_to_0_1(pkt.x),
                                           netfloat_to_0_1(pkt.y),
                                           is_touching ? pressure_or_distance : -1,
                                           is_touching ? -1 : pressure_or_distance,
                                           tilt_x,
                                           tilt_y);
  }
}

void controller_arrival(const CONTROLLER_ARRIVAL_PACKET &pkt,
                        events::StreamSession &session,
                        immer::box<std::shared_ptr<ENetPeer>> connected_client) {
  auto joypads = session.joypads->load();
  if (joypads->find(pkt.controller_number)) {
    // TODO: should we replace it instead?
    logs::log(logs::debug,
              "[INPUT] Received CONTROLLER_ARRIVAL for controller {} which is already present; skipping...",
              pkt.controller_number);
  } else {
    create_new_joypad(session,
                      connected_client,
                      pkt.controller_number,
                      (CONTROLLER_TYPE)pkt.controller_type,
                      pkt.capabilities);
  }
}

void controller_multi(const CONTROLLER_MULTI_PACKET &pkt,
                      events::StreamSession &session,
                      immer::box<std::shared_ptr<ENetPeer>> connected_client) {
  auto joypads = session.joypads->load();
  std::shared_ptr<events::JoypadTypes> selected_pad;
  if (auto joypad = joypads->find(pkt.controller_number)) {
    selected_pad = std::move(*joypad);

    // Check if Moonlight is sending the final packet for this pad
    if (!(pkt.active_gamepad_mask & (1 << pkt.controller_number))) {
      logs::log(logs::debug, "Removing joypad {}", pkt.controller_number);
      // Send the event downstream, Docker will pick it up and remove the device
      events::UnplugDeviceEvent unplug_ev{.session_id = session.session_id};
      std::visit(
          [&unplug_ev](auto &pad) {
            unplug_ev.udev_events = pad.get_udev_events();
            unplug_ev.udev_hw_db_entries = pad.get_udev_hw_db_entries();
          },
          *selected_pad);
      session.event_bus->fire_event(immer::box<events::UnplugDeviceEvent>(unplug_ev));

      // Remove the joypad, this will delete the last reference
      session.joypads->update([&](events::JoypadList joypads) { return joypads.erase(pkt.controller_number); });
    }
  } else {
    // Old Moonlight doesn't support CONTROLLER_ARRIVAL, we create a default pad when it's first mentioned
    selected_pad = create_new_joypad(session, connected_client, pkt.controller_number, XBOX, ANALOG_TRIGGERS | RUMBLE);
  }
  std::visit(
      [pkt](inputtino::Joypad &pad) {
        std::uint16_t bf = pkt.button_flags;
        std::uint32_t bf2 = pkt.buttonFlags2;
        pad.set_pressed_buttons(bf | (bf2 << 16));
        pad.set_stick(inputtino::Joypad::LS, pkt.left_stick_x, pkt.left_stick_y);
        pad.set_stick(inputtino::Joypad::RS, pkt.right_stick_x, pkt.right_stick_y);
        pad.set_triggers(pkt.left_trigger, pkt.right_trigger);
      },
      *selected_pad);
}

void controller_touch(const CONTROLLER_TOUCH_PACKET &pkt, events::StreamSession &session) {
  auto joypads = session.joypads->load();
  std::shared_ptr<events::JoypadTypes> selected_pad;
  if (auto joypad = joypads->find(pkt.controller_number)) {
    selected_pad = std::move(*joypad);
    auto pointer_id = boost::endian::little_to_native(pkt.pointer_id);
    switch (pkt.event_type) {
    case TOUCH_EVENT_DOWN:
    case TOUCH_EVENT_HOVER:
    case TOUCH_EVENT_MOVE: {
      if (std::holds_alternative<PS5Joypad>(*selected_pad)) {
        std::get<PS5Joypad>(*selected_pad)
            .place_finger(pointer_id,
                          netfloat_to_0_1(pkt.x) * (uint16_t)inputtino::PS5Joypad::touchpad_width,
                          netfloat_to_0_1(pkt.y) * (uint16_t)inputtino::PS5Joypad::touchpad_height);
      }
      break;
    }
    case TOUCH_EVENT_UP:
    case TOUCH_EVENT_HOVER_LEAVE:
    case TOUCH_EVENT_CANCEL: {
      if (std::holds_alternative<PS5Joypad>(*selected_pad)) {
        std::get<PS5Joypad>(*selected_pad).release_finger(pointer_id);
      }
      break;
    }
    case TOUCH_EVENT_CANCEL_ALL:
      logs::log(logs::warning, "Received TOUCH_EVENT_CANCEL_ALL which isn't supported");
      break;                      // TODO: remove all fingers
    case TOUCH_EVENT_BUTTON_ONLY: // TODO: ???
      logs::log(logs::warning, "Received TOUCH_EVENT_BUTTON_ONLY which isn't supported");
      break;
    }
  } else {
    logs::log(logs::warning, "Received controller touch for unknown controller {}", pkt.controller_number);
  }
}

void controller_motion(const CONTROLLER_MOTION_PACKET &pkt, events::StreamSession &session) {
  auto joypads = session.joypads->load();
  std::shared_ptr<events::JoypadTypes> selected_pad;
  if (auto joypad = joypads->find(pkt.controller_number)) {
    selected_pad = std::move(*joypad);
    if (std::holds_alternative<PS5Joypad>(*selected_pad)) {
      auto x = utils::from_netfloat(pkt.x);
      auto y = utils::from_netfloat(pkt.y);
      auto z = utils::from_netfloat(pkt.z);

      if (pkt.motion_type == ACCELERATION) {
        std::get<PS5Joypad>(*selected_pad).set_motion(inputtino::PS5Joypad::ACCELERATION, x, y, z);
      } else if (pkt.motion_type == GYROSCOPE) {
        std::get<PS5Joypad>(*selected_pad)
            .set_motion(inputtino::PS5Joypad::GYROSCOPE, deg2rad(x), deg2rad(y), deg2rad(z));
      }
    }
  }
}

void controller_battery(const CONTROLLER_BATTERY_PACKET &pkt, events::StreamSession &session) {
  auto joypads = session.joypads->load();
  std::shared_ptr<events::JoypadTypes> selected_pad;
  if (auto joypad = joypads->find(pkt.controller_number)) {
    selected_pad = std::move(*joypad);
    if (std::holds_alternative<PS5Joypad>(*selected_pad)) {
      inputtino::PS5Joypad::BATTERY_STATE state;
      switch (pkt.battery_state) {
      case BATTERY_STATE_UNKNOWN:
      case BATTERY_NOT_PRESENT:
        return; // We can't set it, let's return
      case BATTERY_DISCHARGHING:
        state = inputtino::PS5Joypad::BATTERY_DISCHARGING;
        break;
      case BATTERY_CHARGING:
        state = inputtino::PS5Joypad::BATTERY_CHARGHING;
        break;
      case BATTERY_NOT_CHARGING:
        state = inputtino::PS5Joypad::CHARGHING_ERROR;
        break;
      case BATTERY_FULL:
        state = inputtino::PS5Joypad::BATTERY_FULL;
        break;
      }
      if (pkt.battery_percentage != BATTERY_PERCENTAGE_UNKNOWN) {
        std::get<PS5Joypad>(*selected_pad).set_battery(state, pkt.battery_percentage);
      }
    }
  }
}

void handle_input(events::StreamSession &session,
                  immer::box<std::shared_ptr<ENetPeer>> connected_client,
                  INPUT_PKT *pkt) {
  switch (pkt->type) {
  case MOUSE_MOVE_REL: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_REL");
    auto move_pkt = static_cast<MOUSE_MOVE_REL_PACKET *>(pkt);
    mouse_move_rel(*move_pkt, session);
    break;
  }
  case MOUSE_MOVE_ABS: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_ABS");
    auto move_pkt = static_cast<MOUSE_MOVE_ABS_PACKET *>(pkt);
    mouse_move_abs(*move_pkt, session);
    break;
  }
  case MOUSE_BUTTON_PRESS:
  case MOUSE_BUTTON_RELEASE: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_BUTTON_PACKET");
    auto btn_pkt = static_cast<MOUSE_BUTTON_PACKET *>(pkt);
    mouse_button(*btn_pkt, session);
    break;
  }
  case MOUSE_SCROLL: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_SCROLL_PACKET");
    auto scroll_pkt = (static_cast<MOUSE_SCROLL_PACKET *>(pkt));
    mouse_scroll(*scroll_pkt, session);
    break;
  }
  case MOUSE_HSCROLL: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_HSCROLL_PACKET");
    auto scroll_pkt = (static_cast<MOUSE_HSCROLL_PACKET *>(pkt));
    mouse_h_scroll(*scroll_pkt, session);
    break;
  }
  case KEY_PRESS:
  case KEY_RELEASE: {
    logs::log(logs::trace, "[INPUT] Received input of type: KEYBOARD_PACKET");
    auto key_pkt = static_cast<KEYBOARD_PACKET *>(pkt);
    keyboard_key(*key_pkt, session);
    break;
  }
  case UTF8_TEXT: {
    logs::log(logs::trace, "[INPUT] Received input of type: UTF8_TEXT");
    auto txt_pkt = static_cast<UTF8_TEXT_PACKET *>(pkt);
    utf8_text(*txt_pkt, session);
    break;
  }
  case TOUCH: {
    logs::log(logs::trace, "[INPUT] Received input of type: TOUCH");
    auto touch_pkt = static_cast<TOUCH_PACKET *>(pkt);
    touch(*touch_pkt, session);
    break;
  }
  case PEN: {
    logs::log(logs::trace, "[INPUT] Received input of type: PEN");
    auto pen_pkt = static_cast<PEN_PACKET *>(pkt);
    pen(*pen_pkt, session);
    break;
  }
  case CONTROLLER_ARRIVAL: {
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_ARRIVAL");
    auto new_controller = static_cast<CONTROLLER_ARRIVAL_PACKET *>(pkt);
    controller_arrival(*new_controller, session, connected_client);
    break;
  }
  case CONTROLLER_MULTI: {
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_MULTI");
    auto controller_pkt = static_cast<CONTROLLER_MULTI_PACKET *>(pkt);
    controller_multi(*controller_pkt, session, connected_client);
    break;
  }
  case CONTROLLER_TOUCH: {
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_TOUCH");
    auto touch_pkt = static_cast<CONTROLLER_TOUCH_PACKET *>(pkt);
    controller_touch(*touch_pkt, session);
    break;
  }
  case CONTROLLER_MOTION: {
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_MOTION");
    auto motion_pkt = static_cast<CONTROLLER_MOTION_PACKET *>(pkt);
    controller_motion(*motion_pkt, session);
    break;
  }
  case CONTROLLER_BATTERY: {
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_BATTERY");
    auto battery_pkt = static_cast<CONTROLLER_BATTERY_PACKET *>(pkt);
    controller_battery(*battery_pkt, session);
    break;
  }
  case HAPTICS:
    logs::log(logs::trace, "[INPUT] Received input of type: HAPTICS");
    break;
  }
}
} // namespace control