#pragma once

#include <chrono>
#include <enet/enet.h>
#include <events/events.hpp>
#include <helpers/logger.hpp>
#include <immer/vector.hpp>
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

/**
 * A stream's session_id is its paired client's identity, so it repeats across reconnects: after a /resume the new
 * ENet peer connects while the dead one is still waiting to time out.
 * Returns the peers, other than `new_peer`, still mapped to `session_id`; they belong to an older stream instance.
 */
immer::vector<ENetPeer *>
superseded_peers(const enet_clients_map &connected_clients, const ENetPeer *new_peer, std::size_t session_id);

/**
 * True when `peer_session` (the stream a peer attached to) has been replaced by a /resume: the running session with
 * the same session_id now carries a different ENet secret. A pause raised by such a peer must not end the new stream.
 */
bool is_superseded_stream(const immer::vector<events::StreamSession> &running_sessions,
                          const events::StreamSession &peer_session);

bool encrypt_and_send(std::string_view payload,
                      std::string_view aes_key,
                      immer::box<std::shared_ptr<ENetPeer>> connected_client);

bool init();

} // namespace control