#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace rtsp {

/**
 * Couples an HTTPS launch/resume with the RTSP handshake that follows it, so
 * two Moonlight clients behind the same NAT don't get their RTSP sessions
 * crossed.
 *
 * Background: below the high-quality-audio threshold moonlight-common-c sends
 * RTSP with `Host: 0.0.0.0`, which makes Wolf's RTSP demux fall back to
 * source-IP matching. For two clients sharing one public IP that matches both
 * to whichever session is first in the list, swapping their AES keys.
 *
 * Fix (the model Sunshine uses): after a launch/resume there is always an RTSP
 * handshake from that same client. We give the launching client an exclusive
 * RTSP "window" for its IP; while the window is open every ambiguous RTSP
 * packet from that IP resolves to its session. A *concurrent* launch from the
 * same IP blocks until the window closes, so the two handshakes never overlap.
 * The very first launch never blocks (the client only opens the RTSP
 * connection after it gets the launch response).
 *
 * The window starts wide (the client may take a moment to connect) and shrinks
 * to a short grace once the first RTSP packet is seen — long enough to cover
 * the rest of that client's OPTIONS/DESCRIBE/SETUP/PLAY burst.
 */
class RtspHandshake {
public:
  static RtspHandshake &instance() {
    static RtspHandshake gate;
    return gate;
  }

  /**
   * Claim the RTSP window for `client_ip`. Returns immediately when no other
   * session from the same IP is mid-handshake; otherwise blocks (up to
   * MAX_WAIT) until that one's window closes. Call right before replying to
   * /launch or /resume.
   */
  void begin(std::size_t session_id, const std::string &client_ip) {
    std::unique_lock<std::mutex> lk(mtx_);
    const auto give_up = clock::now() + MAX_WAIT;
    for (auto it = pending_.find(client_ip); it != pending_.end() && clock::now() < it->second.window_end;
         it = pending_.find(client_ip)) {
      if (clock::now() >= give_up) {
        break; // don't block a launch forever if the previous client vanished
      }
      cv_.wait_until(lk, std::min(it->second.window_end, give_up));
    }
    pending_[client_ip] = Pending{.session_id = session_id, .window_end = clock::now() + INITIAL};
  }

  /**
   * The session that currently owns the RTSP window for `client_ip`, if any.
   * Used by the RTSP demux to resolve `Host: 0.0.0.0` packets. The window stays
   * open for the *whole* handshake: it is closed by complete() on the RTSP PLAY,
   * or by the INITIAL safety timeout if the client never finishes. (An earlier
   * "close on first packet" was too eager — a later SETUP escaped the window and
   * fell back to the wrong session, handing the client the other one's secret.)
   */
  std::optional<std::size_t> active_session(const std::string &client_ip) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = pending_.find(client_ip);
    if (it == pending_.end() || clock::now() >= it->second.window_end) {
      return std::nullopt;
    }
    return it->second.session_id;
  }

  /**
   * Close the RTSP window for a session once its handshake is done (called on
   * the RTSP PLAY), so a same-IP launch waiting on it can proceed immediately.
   */
  void complete(std::size_t session_id) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
      if (it->second.session_id == session_id) {
        pending_.erase(it);
        cv_.notify_all();
        return;
      }
    }
  }

private:
  using clock = std::chrono::steady_clock;
  struct Pending {
    std::size_t session_id;
    clock::time_point window_end;
  };

  std::mutex mtx_;
  std::condition_variable cv_;
  std::map<std::string, Pending> pending_; // keyed by client IP

  // Safety cap: how long a window stays open if the client never sends PLAY
  // (e.g. it connected the HTTPS launch but never started the RTSP handshake).
  static constexpr std::chrono::milliseconds INITIAL{10000};
  // Hard cap on how long a concurrent same-IP launch will block waiting for the
  // in-flight one's handshake to finish.
  static constexpr std::chrono::milliseconds MAX_WAIT{11000};
};

} // namespace rtsp
