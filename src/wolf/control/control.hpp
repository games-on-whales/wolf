#pragma once

#include <helpers/logger.hpp>
#include <range/v3/view.hpp>
#include <state/data-structures.hpp>
#include <thread>

namespace control {

void run_control(const immer::box<state::ControlSession> &control_sess, int timeout_millis = 1000);

bool init();

} // namespace control