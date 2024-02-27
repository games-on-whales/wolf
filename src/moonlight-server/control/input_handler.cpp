#include <boost/endian/conversion.hpp>
#include <boost/locale.hpp>
#include <control/input_handler.hpp>
#include <helpers/logger.hpp>
#include <immer/box.hpp>
#include <platforms/input.hpp>
#include <string>

namespace control {

using namespace wolf::core::input;
using namespace std::string_literals;
using namespace moonlight::control;

std::shared_ptr<Joypad> create_new_joypad(const state::StreamSession &session,
                                          const immer::atom<enet_clients_map> &connected_clients,
                                          int controller_number,
                                          Joypad::CONTROLLER_TYPE type,
                                          uint8_t capabilities) {
  auto joypad = Joypad::create(type, capabilities);
  if (!joypad) {
    logs::log(logs::error, "Failed to create joypad: {}", joypad.getErrorMessage());
    return {};
  }

  auto new_pad = std::make_shared<Joypad>(*joypad);
  new_pad->set_on_rumble([clients = &connected_clients,
                          controller_number,
                          session_id = session.session_id,
                          aes_key = session.aes_key](int low_freq, int high_freq) {
    auto rumble_pkt = ControlRumblePacket{
        .header = {.type = RUMBLE_DATA, .length = sizeof(ControlRumblePacket) - sizeof(ControlPacket)},
        .controller_number = boost::endian::native_to_little((uint16_t)controller_number),
        .low_freq = boost::endian::native_to_little((uint16_t)low_freq),
        .high_freq = boost::endian::native_to_little((uint16_t)high_freq)};
    std::string plaintext = {(char *)&rumble_pkt, sizeof(rumble_pkt)};
    encrypt_and_send(plaintext, aes_key, *clients, session_id);
  });

  if (capabilities & Joypad::ACCELEROMETER) {
    // Request acceleromenter events from the client at 100 Hz
    auto accelerometer_pkt = ControlMotionEventPacket{
        .header{.type = MOTION_EVENT, .length = sizeof(ControlMotionEventPacket) - sizeof(ControlPacket)},
        .controller_number = static_cast<uint16_t>(controller_number),
        .reportrate = 100,
        .type = Joypad::ACCELERATION};
    std::string plaintext = {(char *)&accelerometer_pkt, sizeof(accelerometer_pkt)};
    encrypt_and_send(plaintext, session.aes_key, connected_clients, session.session_id);
  }

  if (capabilities & Joypad::GYRO) {
    // Request gyroscope events from the client at 100 Hz
    auto gyro_pkt = ControlMotionEventPacket{
        .header{.type = MOTION_EVENT, .length = sizeof(ControlMotionEventPacket) - sizeof(ControlPacket)},
        .controller_number = static_cast<uint16_t>(controller_number),
        .reportrate = 100,
        .type = Joypad::GYROSCOPE};
    std::string plaintext = {(char *)&gyro_pkt, sizeof(gyro_pkt)};
    encrypt_and_send(plaintext, session.aes_key, connected_clients, session.session_id);
  }

  session.joypads->update([&](state::JoypadList joypads) {
    logs::log(logs::debug, "[INPUT] Creating joypad {} of type: {}", controller_number, type);

    session.event_bus->fire_event(immer::box<state::PlugDeviceEvent>(
        state::PlugDeviceEvent{.session_id = session.session_id, .device = new_pad}));
    return joypads.set(controller_number, new_pad);
  });
  return new_pad;
}

/**
 * Creates a new PenTablet and saves it into the session;
 * will also trigger a PlugDeviceEvent
 */
std::shared_ptr<PenTablet> create_pen_tablet(state::StreamSession &session) {
  logs::log(logs::debug, "[INPUT] Creating new pen tablet");
  auto tablet = PenTablet::create();
  if (!tablet) {
    logs::log(logs::error, "Failed to create pen tablet: {}", tablet.getErrorMessage());
    return {};
  }
  auto tablet_ptr = std::make_shared<PenTablet>(PenTablet(**tablet));
  session.event_bus->fire_event(immer::box<state::PlugDeviceEvent>(
      state::PlugDeviceEvent{.session_id = session.session_id, .device = tablet_ptr}));
  session.pen_tablet = tablet_ptr;
  return tablet_ptr;
}

/**
 * Creates a new Touch screen and saves it into the session;
 * will also trigger a PlugDeviceEvent
 */
std::shared_ptr<TouchScreen> create_touch_screen(state::StreamSession &session) {
  logs::log(logs::debug, "[INPUT] Creating new touch screen");
  auto touch = TouchScreen::create();
  if (!touch) {
    logs::log(logs::error, "Failed to create touch screen: {}", touch.getErrorMessage());
  }
  auto touch_screen = std::make_shared<TouchScreen>(TouchScreen(**touch));
  session.event_bus->fire_event(immer::box<state::PlugDeviceEvent>(
      state::PlugDeviceEvent{.session_id = session.session_id, .device = touch_screen}));
  session.touch_screen = touch_screen;
  return touch_screen;
}

float netfloat_to_0_1(const utils::netfloat &f) {
  return std::clamp(utils::from_netfloat(f), 0.0f, 1.0f);
}

static inline float deg2rad(float degree) {
  return degree * (M_PI / 180.f);
}

void handle_input(state::StreamSession &session,
                  const immer::atom<enet_clients_map> &connected_clients,
                  INPUT_PKT *pkt) {
  switch (pkt->type) {
    /*
     *  MOUSE
     */
  case MOUSE_MOVE_REL: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_REL");
    auto move_pkt = static_cast<MOUSE_MOVE_REL_PACKET *>(pkt);
    short delta_x = boost::endian::big_to_native(move_pkt->delta_x);
    short delta_y = boost::endian::big_to_native(move_pkt->delta_y);
    session.mouse->move(delta_x, delta_y);
    break;
  }
  case MOUSE_MOVE_ABS: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_ABS");
    auto move_pkt = static_cast<MOUSE_MOVE_ABS_PACKET *>(pkt);
    float x = boost::endian::big_to_native(move_pkt->x);
    float y = boost::endian::big_to_native(move_pkt->y);
    float width = boost::endian::big_to_native(move_pkt->width);
    float height = boost::endian::big_to_native(move_pkt->height);
    session.mouse->move_abs(x, y, width, height);
    break;
  }
  case MOUSE_BUTTON_PRESS:
  case MOUSE_BUTTON_RELEASE: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_BUTTON_PACKET");
    auto btn_pkt = static_cast<MOUSE_BUTTON_PACKET *>(pkt);
    Mouse::MOUSE_BUTTON btn_type;

    switch (btn_pkt->button) {
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
    if (btn_pkt->type == MOUSE_BUTTON_PRESS) {
      session.mouse->press(btn_type);
    } else {
      session.mouse->release(btn_type);
    }
    break;
  }
  case MOUSE_SCROLL: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_SCROLL_PACKET");
    auto scroll_pkt = (static_cast<MOUSE_SCROLL_PACKET *>(pkt));
    session.mouse->vertical_scroll(boost::endian::big_to_native(scroll_pkt->scroll_amt1));
    break;
  }
  case MOUSE_HSCROLL: {
    logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_HSCROLL_PACKET");
    auto scroll_pkt = (static_cast<MOUSE_HSCROLL_PACKET *>(pkt));
    session.mouse->horizontal_scroll(boost::endian::big_to_native(scroll_pkt->scroll_amount));
    break;
  }
    /*
     *  KEYBOARD
     */
  case KEY_PRESS:
  case KEY_RELEASE: {
    logs::log(logs::trace, "[INPUT] Received input of type: KEYBOARD_PACKET");
    auto key_pkt = static_cast<KEYBOARD_PACKET *>(pkt);
    // moonlight always sets the high bit; not sure why but mask it off here
    short moonlight_key = (short)boost::endian::little_to_native(key_pkt->key_code) & (short)0x7fff;
    if (key_pkt->type == KEY_PRESS) {
      session.keyboard->press(moonlight_key);
    } else {
      session.keyboard->release(moonlight_key);
    }
    break;
  }
  case UTF8_TEXT: {
    logs::log(logs::trace, "[INPUT] Received input of type: UTF8_TEXT");
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
    auto txt_pkt = static_cast<UTF8_TEXT_PACKET *>(pkt);
    auto size = boost::endian::big_to_native(txt_pkt->data_size) - sizeof(txt_pkt->packet_type) - 2;
    /* Reading input text as UTF-8 */
    auto utf8 = boost::locale::conv::to_utf<wchar_t>(txt_pkt->text, txt_pkt->text + size, "UTF-8");
    /* Converting to UTF-32 */
    auto utf32 = boost::locale::conv::utf_to_utf<char32_t>(utf8);
    wolf::platforms::input::paste_utf(session.keyboard, utf32);
    break;
  }
  case TOUCH: {
    logs::log(logs::trace, "[INPUT] Received input of type: TOUCH");
    if (!session.touch_screen) {
      create_touch_screen(session);
    }
    auto touch_pkt = static_cast<TOUCH_PACKET *>(pkt);

    auto finger_id = boost::endian::little_to_native(touch_pkt->pointer_id);
    auto x = netfloat_to_0_1(touch_pkt->x);
    auto y = netfloat_to_0_1(touch_pkt->y);
    auto pressure_or_distance = netfloat_to_0_1(touch_pkt->pressure_or_distance);
    switch (touch_pkt->event_type) {
    case pkts::TOUCH_EVENT_HOVER:
    case pkts::TOUCH_EVENT_DOWN:
    case pkts::TOUCH_EVENT_MOVE: {
      // Convert our 0..360 range to -90..90 relative to Y axis
      int adjusted_angle = touch_pkt->rotation;

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
      session.touch_screen->place_finger(finger_id, x, y, pressure_or_distance, adjusted_angle);
      break;
    }
    case pkts::TOUCH_EVENT_UP:
    case pkts::TOUCH_EVENT_HOVER_LEAVE:
    case pkts::TOUCH_EVENT_CANCEL:
      session.touch_screen->release_finger(finger_id);
      break;
    default:
      logs::log(logs::warning, "[INPUT] Unknown touch event type {}", touch_pkt->event_type);
    }
    break;
  }
  case PEN: {
    logs::log(logs::trace, "[INPUT] Received input of type: PEN");
    if (!session.pen_tablet) {
      create_pen_tablet(session);
    }
    auto pen_pkt = static_cast<PEN_PACKET *>(pkt);

    // First set the buttons
    session.pen_tablet->set_btn(PenTablet::PRIMARY, pen_pkt->pen_buttons & PEN_BUTTON_TYPE_PRIMARY);
    session.pen_tablet->set_btn(PenTablet::SECONDARY, pen_pkt->pen_buttons & PEN_BUTTON_TYPE_SECONDARY);
    session.pen_tablet->set_btn(PenTablet::TERTIARY, pen_pkt->pen_buttons & PEN_BUTTON_TYPE_TERTIARY);

    // Set the tool
    PenTablet::TOOL_TYPE tool;
    switch (pen_pkt->tool_type) {
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

    auto pressure_or_distance = netfloat_to_0_1(pen_pkt->pressure_or_distance);

    // Normalize rotation value to 0-359 degree range
    auto rotation = boost::endian::little_to_native(pen_pkt->rotation);
    if (rotation != PEN_ROTATION_UNKNOWN) {
      rotation %= 360;
    }

    // Here we receive:
    //  - Rotation: degrees from vertical in Y dimension (parallel to screen, 0..360)
    //  - Tilt: degrees from vertical in Z dimension (perpendicular to screen, 0..90)
    float tilt_x = 0;
    float tilt_y = 0;
    // Convert polar coordinates into Y tilt angles
    if (pen_pkt->tilt != PEN_TILT_UNKNOWN && rotation != PEN_ROTATION_UNKNOWN) {
      auto rotation_rads = deg2rad(rotation);
      auto tilt_rads = deg2rad(pen_pkt->tilt);
      auto r = std::sin(tilt_rads);
      auto z = std::cos(tilt_rads);

      tilt_x = std::atan2(std::sin(-rotation_rads) * r, z) * 180.f / M_PI;
      tilt_y = std::atan2(std::cos(-rotation_rads) * r, z) * 180.f / M_PI;
    }

    session.pen_tablet->place_tool(tool,
                                   netfloat_to_0_1(pen_pkt->x),
                                   netfloat_to_0_1(pen_pkt->y),
                                   pen_pkt->event_type == TOUCH_EVENT_DOWN ? pressure_or_distance : -1,
                                   pen_pkt->event_type == TOUCH_EVENT_HOVER ? pressure_or_distance : -1,
                                   tilt_x,
                                   tilt_y);

    break;
  }
    /*
     *  CONTROLLER
     */
  case CONTROLLER_ARRIVAL: {
    auto new_controller = static_cast<CONTROLLER_ARRIVAL_PACKET *>(pkt);
    auto joypads = session.joypads->load();
    if (joypads->find(new_controller->controller_number)) {
      // TODO: should we replace it instead?
      logs::log(logs::debug,
                "[INPUT] Received CONTROLLER_ARRIVAL for controller {} which is already present; skipping...",
                new_controller->controller_number);
    } else {
      create_new_joypad(session,
                        connected_clients,
                        new_controller->controller_number,
                        (Joypad::CONTROLLER_TYPE)new_controller->controller_type,
                        new_controller->capabilities);
    }
    break;
  }
  case CONTROLLER_MULTI: {
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_MULTI");
    auto controller_pkt = static_cast<CONTROLLER_MULTI_PACKET *>(pkt);
    auto joypads = session.joypads->load();
    std::shared_ptr<Joypad> selected_pad;
    if (auto joypad = joypads->find(controller_pkt->controller_number)) {
      selected_pad = std::move(*joypad);

      // Check if Moonlight is sending the final packet for this pad
      if (!(controller_pkt->active_gamepad_mask & (1 << controller_pkt->controller_number))) {
        logs::log(logs::debug, "Removing joypad {}", controller_pkt->controller_number);
        // Send the event downstream, Docker will pick it up and remove the device
        session.event_bus->fire_event(immer::box<state::UnplugDeviceEvent>(
            state::UnplugDeviceEvent{.session_id = session.session_id, .device = selected_pad}));
        // Remove the joypad, this will delete the last reference
        session.joypads->update(
            [&](state::JoypadList joypads) { return joypads.erase(controller_pkt->controller_number); });
      }
    } else {
      // Old Moonlight versions don't support CONTROLLER_ARRIVAL, we create a default pad when it's first mentioned
      selected_pad = create_new_joypad(session,
                                       connected_clients,
                                       controller_pkt->controller_number,
                                       Joypad::XBOX,
                                       Joypad::ANALOG_TRIGGERS | Joypad::RUMBLE);
    }
    selected_pad->set_pressed_buttons(controller_pkt->button_flags | (controller_pkt->buttonFlags2 << 16));
    selected_pad->set_stick(Joypad::LS, controller_pkt->left_stick_x, controller_pkt->left_stick_y);
    selected_pad->set_stick(Joypad::RS, controller_pkt->right_stick_x, controller_pkt->right_stick_y);
    selected_pad->set_triggers(controller_pkt->left_trigger, controller_pkt->right_trigger);
    break;
  }
  case CONTROLLER_TOUCH: {
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_TOUCH");
    auto touch_pkt = static_cast<CONTROLLER_TOUCH_PACKET *>(pkt);
    auto joypads = session.joypads->load();
    std::shared_ptr<Joypad> selected_pad;
    if (auto joypad = joypads->find(touch_pkt->controller_number)) {
      selected_pad = std::move(*joypad);
      auto pointer_id = boost::endian::little_to_native(touch_pkt->pointer_id);
      switch (touch_pkt->event_type) {
      case TOUCH_EVENT_DOWN:
      case TOUCH_EVENT_HOVER:
      case TOUCH_EVENT_MOVE: {
        // TODO: Moonlight seems to always pass 1.0 (0x0000803f little endian)
        // Values too high will be discarded by libinput as detecting palm pressure
        if (auto trackpad = selected_pad->get_trackpad()) {
          auto pressure = std::clamp(utils::from_netfloat(touch_pkt->pressure), 0.0f, 0.5f);
          trackpad->place_finger(pointer_id, netfloat_to_0_1(touch_pkt->x), netfloat_to_0_1(touch_pkt->y), pressure, 0);
        }
        break;
      }
      case TOUCH_EVENT_UP:
      case TOUCH_EVENT_HOVER_LEAVE:
      case TOUCH_EVENT_CANCEL: {
        if (auto trackpad = selected_pad->get_trackpad()) {
          trackpad->release_finger(pointer_id);
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
      logs::log(logs::warning, "Received controller touch for unknown controller {}", touch_pkt->controller_number);
    }
    break;
  }
  case CONTROLLER_MOTION: {
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_MOTION");
    auto motion_pkt = static_cast<CONTROLLER_MOTION_PACKET *>(pkt);
    auto joypads = session.joypads->load();
    std::shared_ptr<Joypad> selected_pad;
    if (auto joypad = joypads->find(motion_pkt->controller_number)) {
      selected_pad = std::move(*joypad);
      selected_pad->set_motion(motion_pkt->motion_type,
                               utils::from_netfloat(motion_pkt->x),
                               utils::from_netfloat(motion_pkt->y),
                               utils::from_netfloat(motion_pkt->z));
    }
    break;
  }
  case CONTROLLER_BATTERY:
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_BATTERY");
    break;
  case HAPTICS:
    logs::log(logs::trace, "[INPUT] Received input of type: HAPTICS");
    break;
  }
}

} // namespace control