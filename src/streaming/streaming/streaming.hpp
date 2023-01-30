#pragma once
#include <fmt/core.h>
#include <fmt/format.h>
#include <gst/gst.h>
#include <immer/box.hpp>
#include <moonlight/data-structures.hpp>
#include <streaming/data-structures.hpp>

namespace streaming {

using namespace moonlight::control;
void init();

/**
 * @return the Gstreamer version we are linked to
 */
inline std::string version() {
  guint major, minor, micro, nano;
  gst_version(&major, &minor, &micro, &nano);
  return fmt::format("{}.{}.{}-{}", major, minor, micro, nano);
}

void start_streaming_video(immer::box<state::VideoSession> video_session, unsigned short client_port);

void start_streaming_audio(immer::box<state::AudioSession> audio_session, unsigned short client_port);

} // namespace streaming