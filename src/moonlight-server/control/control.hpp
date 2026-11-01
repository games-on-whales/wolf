#pragma once

#include <chrono>
#include <enet/enet.h>
#include <events/events.hpp>
#include <helpers/logger.hpp>
#include <moonlight/control.hpp>
#include <range/v3/view.hpp>
#include <state/data-structures.hpp>
#include <thread>

namespace control {

using namespace std::chrono_literals;
using namespace wolf::core;

void run_control(int port,
                 const state::SessionsAtoms &running_sessions,
                 const std::shared_ptr<events::EventBusType> &event_bus,
                 int peers = 20,
                 std::chrono::milliseconds timeout = 1000ms,
                 const std::string &host_ip = "0.0.0.0");

using enet_clients_map = immer::map<ENetPeer *, immer::box<events::StreamSession>>;

std::shared_ptr<ENetPeer> to_shared_ptr(ENetPeer *peer);

bool encrypt_and_send(std::string_view payload,
                      std::string_view aes_key,
                      immer::box<std::shared_ptr<ENetPeer>> connected_client);

bool init();

} // namespace control