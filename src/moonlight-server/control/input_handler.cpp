#include <boost/endian/conversion.hpp>
#include <boost/locale.hpp>
#include <control/input_handler.hpp>
#include <helpers/logger.hpp>
#include <moonlight/control.hpp>
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
  auto new_pad = std::make_shared<Joypad>(type, capabilities);

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

  session.joypads->update([&](state::JoypadList joypads) {
    logs::log(logs::debug, "[INPUT] Creating joypad {} of type: {}", controller_number, type);
    // TODO: trigger event in the event bus so that Docker can pick this up
    if (auto old_controller = joypads.find(controller_number)) {
      logs::log(logs::debug, "[INPUT] Replacing previously plugged joypad");
      // TODO: should we do something with old_controller?
    }
    return joypads.set(controller_number, new_pad);
  });
  return new_pad;
}

float netfloat_to_0_1(utils::netfloat f) {
  return std::clamp(utils::from_netfloat(f), 0.0f, 1.0f);
}

void handle_input(const state::StreamSession &session,
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
    auto txt_pkt = static_cast<UTF8_TEXT_PACKET *>(pkt);
    auto size = boost::endian::big_to_native(txt_pkt->data_size) - sizeof(txt_pkt->packet_type) - 2;
    /* Reading input text as UTF-8 */
    auto utf8 = boost::locale::conv::to_utf<wchar_t>(txt_pkt->text, txt_pkt->text + size, "UTF-8");
    /* Converting to UTF-32 */
    auto utf32 = boost::locale::conv::utf_to_utf<char32_t>(utf8);
    session.keyboard->paste_utf(utf32);
    break;
  }
  case TOUCH:
    logs::log(logs::trace, "[INPUT] Received input of type: TOUCH");
    break;
  case PEN:
    logs::log(logs::trace, "[INPUT] Received input of type: PEN");
    break;
    /*
     *  CONTROLLER
     */
  case CONTROLLER_ARRIVAL: {
    auto new_controller = static_cast<CONTROLLER_ARRIVAL_PACKET *>(pkt);
    create_new_joypad(session,
                      connected_clients,
                      new_controller->controller_number,
                      (Joypad::CONTROLLER_TYPE)new_controller->controller_type,
                      new_controller->capabilities);
    break;
  }
  case CONTROLLER_MULTI: {
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_MULTI");
    auto controller_pkt = static_cast<CONTROLLER_MULTI_PACKET *>(pkt);
    auto joypads = session.joypads->load();
    std::shared_ptr<Joypad> selected_pad;
    if (auto joypad = joypads->find(controller_pkt->controller_number)) {
      selected_pad = std::move(*joypad);
    } else {
      // Old Moonlight versions don't support CONTROLLER_ARRIVAL, we create a default pad when it's first mentioned
      selected_pad = create_new_joypad(session,
                                       connected_clients,
                                       controller_pkt->controller_number,
                                       Joypad::XBOX,
                                       Joypad::ANALOG_TRIGGERS | Joypad::RUMBLE);
    }
    selected_pad->set_pressed_buttons(controller_pkt->button_flags | (controller_pkt->buttonFlags2 << 16));
    selected_pad->set_stick(Joypad::L2, controller_pkt->left_stick_x, controller_pkt->left_stick_y);
    selected_pad->set_stick(Joypad::R2, controller_pkt->right_stick_x, controller_pkt->right_stick_y);
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
      case TOUCH_EVENT_MOVE:
        selected_pad->touchpad_place_finger(pointer_id, netfloat_to_0_1(touch_pkt->x), netfloat_to_0_1(touch_pkt->y));
        break;
      case TOUCH_EVENT_UP:
      case TOUCH_EVENT_HOVER_LEAVE:
        selected_pad->touchpad_release_finger(pointer_id);
        break;
      case TOUCH_EVENT_CANCEL:
        selected_pad->touchpad_release_finger(pointer_id);
        break;
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
  case CONTROLLER_MOTION:
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_MOTION");
    break;
  case CONTROLLER_BATTERY:
    logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_BATTERY");
    break;
  case HAPTICS:
    logs::log(logs::trace, "[INPUT] Received input of type: HAPTICS");
    break;
  }
}

} // namespace control