#pragma once
#include <boost/asio.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <gst/gst.h>
#include <immer/box.hpp>
#include <memory>
#include <moonlight/data-structures.hpp>
#include <streaming/data-structures.hpp>
#include <streaming/virtual-display.hpp>

namespace streaming {

using namespace moonlight::control;
void init();

/**
 * @return the Gstreamer version we are linked to
 */
inline std::string get_gst_version() {
  guint major, minor, micro, nano;
  gst_version(&major, &minor, &micro, &nano);
  return fmt::format("{}.{}.{}-{}", major, minor, micro, nano);
}

void start_streaming_video(const immer::box<state::VideoSession> &video_session,
                           const std::shared_ptr<dp::event_bus> &event_bus,
                           const std::shared_ptr<WaylandState> &wl_state,
                           unsigned short client_port);

void start_streaming_audio(const immer::box<state::AudioSession> &audio_session,
                           const std::shared_ptr<dp::event_bus> &event_bus,
                           unsigned short client_port,
                           const std::string &sink_name,
                           const std::string &server_name);

} // namespace streaming