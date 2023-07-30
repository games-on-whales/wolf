#pragma once

#include <control/control.hpp>
#include <moonlight/control.hpp>
#include <state/data-structures.hpp>

namespace control {

using namespace moonlight::control::pkts;

void handle_input(const state::StreamSession &session,
                  const immer::atom<enet_clients_map> &connected_clients,
                  INPUT_PKT *pkt);

} // namespace control
