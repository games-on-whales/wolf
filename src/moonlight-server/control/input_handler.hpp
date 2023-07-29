#pragma once

#include <moonlight/control.hpp>
#include <state/data-structures.hpp>

namespace control {

using namespace moonlight::control::pkts;

void handle_input(const state::StreamSession &session, INPUT_PKT *pkt);

} // namespace control
