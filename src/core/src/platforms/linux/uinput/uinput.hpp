/**
 * This is all based on libevdev
 *  - Here's a great introductory blog post:
 * https://web.archive.org/web/20200809000852/https://who-t.blogspot.com/2016/09/understanding-evdev.html/
 *  - Main docs: https://www.freedesktop.org/software/libevdev/doc/latest/index.html
 *  - Python docs are also of good quality: https://python-libevdev.readthedocs.io/en/latest/index.html
 *
 * You can debug your system using `evemu-describe`, `evemu-record` and `udevadm monitor`
 * (they can be installed using: `apt install -y evemu-tools`)
 *
 * For controllers there's a set of tools in the `joystick` package:
 * - ffcfstress  - force-feedback stress test
 * - ffmvforce   - force-feedback orientation test
 * - ffset       - force-feedback configuration tool
 * - fftest      - general force-feedback test
 * - jstest      - joystick test
 * - jscal       - joystick calibration tool
 *
 * For force feedback see: https://www.kernel.org/doc/html/latest/input/ff.html
 */

#include <core/input.hpp>
#include <immer/array.hpp>
#include <immer/atom.hpp>
#include <iomanip>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <libudev.h>
#include <memory>
#include <optional>
#include <string>

namespace wolf::core::input {

using libevdev_ptr = std::shared_ptr<libevdev>;
using libevdev_uinput_ptr = std::shared_ptr<libevdev_uinput>;
using libevdev_event_ptr = std::shared_ptr<input_event>;

/**
 * Given a device will read all queued events available at this time up to max_events
 * It'll automatically discard all EV_SYN events
 *
 * @returns a list of smart pointers of evdev input_event (empty when no events are available)
 */
std::vector<libevdev_event_ptr> fetch_events(const libevdev_ptr &dev, int max_events = 50);

/**
 * Given a uinput fd will read all queued events available at this time up to max_events
 */
std::vector<libevdev_event_ptr> fetch_events(int uinput_fd, int max_events = 50);

/**
 * Takes an UTF-32 encoded string and returns a hex string representation of the bytes (uppercase)
 *
 * ex: ['💩'] = "1F4A9" // see UTF encoding at https://www.compart.com/en/unicode/U+1F4A9
 *
 * adapted from: https://stackoverflow.com/a/7639754
 */
static std::string to_hex(const std::basic_string<char32_t> &str) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (const auto &ch : str) {
    ss << ch;
  }

  std::string hex_unicode(ss.str());
  std::transform(hex_unicode.begin(), hex_unicode.end(), hex_unicode.begin(), ::toupper);
  return hex_unicode;
}

} // namespace wolf::core::input
