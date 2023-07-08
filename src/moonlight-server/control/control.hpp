#pragma once

#include <chrono>
#include <helpers/logger.hpp>
#include <range/v3/view.hpp>
#include <state/data-structures.hpp>
#include <thread>

namespace control {

using namespace std::chrono_literals;

void run_control(int port,
                 const state::SessionsAtoms &running_sessions,
                 const std::shared_ptr<dp::event_bus> &event_bus,
                 int peers = 20,
                 std::chrono::milliseconds timeout = 1000ms,
                 const std::string &host_ip = "0.0.0.0");

bool init();

} // namespace control