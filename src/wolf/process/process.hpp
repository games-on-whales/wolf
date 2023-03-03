#pragma once
#include <boost/process.hpp>
#include <eventbus/event_bus.hpp>
#include <immer/box.hpp>
#include <memory>
#include <state/data-structures.hpp>
#include <streaming/data-structures.hpp>
#include <string>
#include <thread>

namespace process {

namespace bp = boost::process;

void run_process(const std::shared_ptr<dp::event_bus> &event_bus,
                 const state::StreamSession &running_session,
                 const state::SocketReadyEV &video_sockets,
                 const std::string &audio_server);

} // namespace process