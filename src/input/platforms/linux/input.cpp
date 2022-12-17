#include <boost/endian/conversion.hpp>
#include <helpers/logger.hpp>
#include <input/input.hpp>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

namespace input {

using namespace moonlight::control;

using libevdev_ptr = std::shared_ptr<libevdev>;
using libevdev_uinput_ptr = std::shared_ptr<libevdev_uinput>;

constexpr int ABS_MAX_WIDTH = 1920;
constexpr int ABS_MAX_HEIGHT = 1080;

std::optional<libevdev_uinput *> create_mouse(libevdev *dev) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf Mouse");
  libevdev_set_name(dev, "Wolf mouse virtual device");
  libevdev_set_id_vendor(dev, 0xAB01);
  libevdev_set_id_product(dev, 0xAB02);
  libevdev_set_id_version(dev, 0xAB03);
  libevdev_set_id_bustype(dev, BUS_USB);

  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_RIGHT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_MIDDLE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_SIDE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_EXTRA, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_FORWARD, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_BACK, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TASK, nullptr);

  libevdev_enable_event_type(dev, EV_REL);
  libevdev_enable_event_code(dev, EV_REL, REL_X, nullptr);
  libevdev_enable_event_code(dev, EV_REL, REL_Y, nullptr);

  libevdev_enable_event_type(dev, EV_MSC);
  libevdev_enable_event_code(dev, EV_MSC, MSC_SCAN, nullptr);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create mouse device, error code: {}", err);
    return {};
  }

  return uidev;
}

std::optional<libevdev_uinput *> create_touchpad(libevdev *dev) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf Touchpad");
  libevdev_set_name(dev, "Wolf touchpad virtual device");
  libevdev_set_id_vendor(dev, 0xAB11);
  libevdev_set_id_product(dev, 0xAB12);
  libevdev_set_id_version(dev, 0xAB13);
  libevdev_set_id_bustype(dev, BUS_USB);

  libevdev_enable_property(dev, INPUT_PROP_DIRECT);
  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOUCH, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOOL_PEN, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOOL_FINGER, nullptr);

  struct input_absinfo absinfo {
    .value = 0, .minimum = 0, .maximum = 0, .fuzz = 1, .flat = 0, .resolution = 40
  };
  libevdev_enable_event_type(dev, EV_ABS);

  absinfo.maximum = ABS_MAX_WIDTH;
  libevdev_enable_event_code(dev, EV_ABS, ABS_X, &absinfo);
  absinfo.maximum = ABS_MAX_HEIGHT;
  libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &absinfo);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create mouse device, error code: {}", err);
    return {};
  }

  return uidev;
}

void move_mouse(libevdev_uinput *mouse, const data::MOUSE_MOVE_REL_PACKET &move_pkt) {
  if (move_pkt.delta_x) {
    libevdev_uinput_write_event(mouse, EV_REL, REL_X, move_pkt.delta_x);
  }

  if (move_pkt.delta_y) {
    libevdev_uinput_write_event(mouse, EV_REL, REL_Y, move_pkt.delta_y);
  }

  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

void move_touchpad(libevdev_uinput *mouse, const data::MOUSE_MOVE_ABS_PACKET &move_pkt) {
  float x = boost::endian::big_to_native(move_pkt.x);
  float y = boost::endian::big_to_native(move_pkt.y);
  float width = boost::endian::big_to_native(move_pkt.width);
  float height = boost::endian::big_to_native(move_pkt.height);

  int scaled_x = (int) std::lround((ABS_MAX_WIDTH / width) * x);
  int scaled_y = (int) std::lround((ABS_MAX_HEIGHT / height) * y);

  libevdev_uinput_write_event(mouse, EV_ABS, ABS_X, scaled_x);
  libevdev_uinput_write_event(mouse, EV_ABS, ABS_Y, scaled_y);
  libevdev_uinput_write_event(mouse, EV_KEY, BTN_TOOL_FINGER, 1);
  libevdev_uinput_write_event(mouse, EV_KEY, BTN_TOOL_FINGER, 0);

  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

void mouse_press(libevdev_uinput *mouse, const data::MOUSE_BUTTON_PACKET &btn_pkt) {
  int btn_type;
  int scan;
  auto constexpr BUTTON_RELEASED = 0x09;
  auto release = btn_pkt.action == BUTTON_RELEASED;

  if (btn_pkt.button == 1) {
    btn_type = BTN_LEFT;
    scan = 90001;
  } else if (btn_pkt.button == 2) {
    btn_type = BTN_MIDDLE;
    scan = 90003;
  } else if (btn_pkt.button == 3) {
    btn_type = BTN_RIGHT;
    scan = 90002;
  } else if (btn_pkt.button == 4) {
    btn_type = BTN_SIDE;
    scan = 90004;
  } else {
    btn_type = BTN_EXTRA;
    scan = 90005;
  }

  libevdev_uinput_write_event(mouse, EV_MSC, MSC_SCAN, scan);
  libevdev_uinput_write_event(mouse, EV_KEY, btn_type, release ? 0 : 1);
  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

immer::array<immer::box<dp::handler_registration>> setup_handlers(std::size_t session_id,
                                                                  std::shared_ptr<dp::event_bus> event_bus) {
  logs::log(logs::debug, "Setting up input handlers for session: {}", session_id);

  libevdev_ptr mouse_dev(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr mouse_ptr(create_mouse(mouse_dev.get()).value(),
                                ::libevdev_uinput_destroy); // TODO: handle unable to create mouse

  libevdev_ptr touch_dev(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr touch_ptr(create_touchpad(touch_dev.get()).value(),
                                ::libevdev_uinput_destroy); // TODO: handle unable to create touchpad

  auto ctrl_handler = event_bus->register_handler<immer::box<ControlEvent>>(
      [sess_id = session_id, mouse_ptr, touch_ptr](immer::box<ControlEvent> ctrl_ev) {
        if (ctrl_ev->session_id == sess_id && ctrl_ev->type == INPUT_DATA) {
          auto input = (const data::INPUT_PKT *)(ctrl_ev->raw_packet.data());

          switch ((int)boost::endian::big_to_native((int)input->type)) {
          case data::MOUSE_MOVE_REL:
            logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_REL");
            move_mouse(mouse_ptr.get(), *(data::MOUSE_MOVE_REL_PACKET *)input);
            break;
          case data::MOUSE_MOVE_ABS:
            logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_ABS");
            move_touchpad(touch_ptr.get(), *(data::MOUSE_MOVE_ABS_PACKET *)input);
            break;
          case data::MOUSE_BUTTON:
            logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_BUTTON");
            mouse_press(mouse_ptr.get(), *(data::MOUSE_BUTTON_PACKET *)input);
            break;
          case data::CONTROLLER_MULTI:
            logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_MULTI");
            break;
          case data::CONTROLLER:
            logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER");
            break;
          case data::KEYBOARD_OR_SCROLL:
            logs::log(logs::trace, "[INPUT] Received input of type: KEYBOARD_OR_SCROLL");
            break;
          }
        }
      });

  return immer::array<immer::box<dp::handler_registration>>{std::move(ctrl_handler)};
}
} // namespace input