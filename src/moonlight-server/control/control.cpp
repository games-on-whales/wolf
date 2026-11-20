#include "core/input.hpp"
#include <algorithm>
#include <control/control.hpp>
#include <control/input_handler.hpp>
#include <events/events.hpp>
#include <helpers/tsqueue.hpp>
#include <immer/box.hpp>
#include <state/sessions.hpp>
#include <sys/socket.h>

namespace control {

using namespace ranges;
using namespace moonlight::control;
using namespace wolf::core::events;

void free_host(ENetHost *host) {
  std::for_each(host->peers, host->peers + host->peerCount, [](ENetPeer &peer_ref) {
    ENetPeer *peer = &peer_ref;

    if (peer) {
      enet_peer_disconnect_now(peer, 0);
    }
  });

  enet_host_destroy(host);
}

using enet_host = std::unique_ptr<ENetHost, decltype(&free_host)>;
using enet_packet = std::unique_ptr<ENetPacket, decltype(&enet_packet_destroy)>;

bool init() {
  auto error_code = enet_initialize();
  if (error_code != 0) {
    logs::log(logs::error, "An error occurred while initializing Enet: {}.", error_code);
    return false;
  }
  return true;
}

enet_host create_host(std::string_view host, std::uint16_t port, std::size_t peers) {
  ENetAddress addr;
  enet_address_set_host(&addr, host.data());
  enet_address_set_port(&addr, port);

  auto enet_host = enet_host_create(AF_INET, &addr, peers, 0, 0, 0);
  if (enet_host == nullptr) {
    logs::log(logs::error, "An error occurred while trying to create an ENet server host.");
  }

  return {enet_host, free_host};
}

/**
 * The Moonlight fork of ENET doesn't include host and port as easily accessible parts of the struct,
 * we have to extract them manually
 */
std::pair<std::string /* ip */, int /* port */> get_ip(const sockaddr *const ip_addr) {
  char data[INET6_ADDRSTRLEN];

  auto family = ip_addr->sa_family;
  std::uint16_t port;
  if (family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6 *)ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
    port = ((sockaddr_in6 *)ip_addr)->sin6_port;
  }

  if (family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in *)ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
    port = ((sockaddr_in *)ip_addr)->sin_port;
  }

  return {std::string{data}, port};
}

bool send_packet(std::string_view payload, ENetPeer *peer) {
  logs::log(logs::trace, "[ENET] Sending packet");
  auto packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
  if (enet_peer_send(peer, 0, packet) < 0) {
    logs::log(logs::warning, "[ENET] Failed to send packet");
    enet_packet_destroy(packet);
    return false;
  }
  return true;
}

static void encrypt_and_send(std::string_view payload, const StreamSession &session, ENetPeer *peer) {
  const auto mode = session.control_encryption->mode();
  if (mode == ControlMode::pending) {
    return;
  }
  const auto sequence = session.control_encryption->next_host_sequence();
  if (!sequence) {
    logs::log(logs::warning, "[ENET] Control V2 sequence exhausted; reconnect to rekey");
    return;
  }
  try {
    auto encrypted = control::encrypt_packet(session.aes_key, *sequence, payload, mode);
    send_packet({reinterpret_cast<const char *>(encrypted.get()), encrypted->full_size()}, peer);
  } catch (const std::runtime_error &e) {
    logs::log(logs::warning, "[ENET] Unable to encrypt outgoing packet: {}", e.what());
  }
}

static std::optional<events::StreamSession> find_connecting_session(const state::SessionsAtoms &running_sessions,
                                                                    std::string_view client_ip,
                                                                    enet_uint32 connect_data) {
  const auto sessions = running_sessions->load();
  for (const StreamSession &session : *sessions) {
    if (session.enet_secret_payload == connect_data) {
      return session;
    }
  }
  for (const StreamSession &session : *sessions) {
    if (session.control_encryption->mode() == ControlMode::legacy && session.ip == client_ip) {
      return session;
    }
  }
  return std::nullopt;
}

static bool is_current_connection(const state::SessionsAtoms &running_sessions, const StreamSession &bound) {
  const auto current = state::get_session_by_id(*running_sessions->load(), bound.session_id);
  return current && current->control_encryption == bound.control_encryption;
}

void run_control(int port,
                 const state::SessionsAtoms &running_sessions,
                 const std::shared_ptr<events::EventBusType> &event_bus,
                 int peers,
                 std::chrono::milliseconds timeout,
                 const std::string &host_ip) {

  enet_host host = create_host(host_ip, port, peers);
  logs::log(logs::info, "Control server started on port: {}", port);

  ENetEvent event;

  // The ENet loop is the sole writer. Stop handlers use a snapshot so queued
  // termination follows the exact peer binding, even if ENet reuses its slot.
  auto connected_clients = std::make_shared<immer::atom<enet_clients_map>>();

  struct FeedbackMessage {
    std::size_t session_id;
    std::string payload;
    std::shared_ptr<immer::atom<JoypadList>> joypads;
  };
  struct Termination {
    ENetPeer *peer;
    immer::box<StreamSession> binding;
  };
  constexpr std::size_t queue_capacity = 256;
  constexpr int messages_per_iteration = 16;
  auto outgoing = std::make_shared<TSQueue<FeedbackMessage>>();
  auto terminations = std::make_shared<TSQueue<Termination>>();

  auto stop_ev = event_bus->register_handler<immer::box<StopStreamEvent>>(
      [connections = std::weak_ptr{connected_clients},
       queue = std::weak_ptr{terminations}](const immer::box<StopStreamEvent> &ev) {
        auto active = connections.lock();
        auto pending = queue.lock();
        if (!active || !pending) {
          return;
        }
        const auto snapshot = active->load();
        for (const auto &[peer, binding] : *snapshot) {
          if (binding->session_id == ev->session_id) {
            if (!pending->try_push({peer, binding}, queue_capacity)) {
              logs::log(logs::warning, "[ENET] Termination queue full");
            }
            return;
          }
        }
      });

  while (true) {
    const auto connections = connected_clients->load();
    for (auto count = 0; count < messages_per_iteration; ++count) {
      const auto stop = terminations->pop(0ms);
      if (!stop) {
        break;
      }
      if (const auto active = connections->find(stop->peer); active && &active->get() == &stop->binding.get()) {
        const auto packet = ControlTerminatePacket{};
        encrypt_and_send({reinterpret_cast<const char *>(&packet), sizeof(packet)}, **active, stop->peer);
      }
    }

    // Joypads survive resume. Route feedback to the current peer for the
    // same device set, and bound work so ENet can continue servicing input.
    for (auto count = 0; count < messages_per_iteration; ++count) {
      const auto message = outgoing->pop(0ms);
      if (!message) {
        break;
      }
      for (const auto &[peer, session] : *connections) {
        if (session->session_id == message->session_id && message->joypads == session->joypads &&
            is_current_connection(running_sessions, *session)) {
          encrypt_and_send(message->payload, *session, peer);
          break;
        }
      }
    }
    // Keep feedback responsive while connected, and keep the idle wait otherwise.
    const auto wait = connections->empty() ? timeout : std::min(timeout, 8ms);
    if (enet_host_service(host.get(), &event, wait.count()) > 0) {
      auto [client_ip, client_port] = get_ip((sockaddr *)&event.peer->address.address);
      if (event.type == ENET_EVENT_TYPE_CONNECT) {
        auto session = find_connecting_session(running_sessions, client_ip, event.data);
        if (!session || !session->control_encryption->claim_peer()) {
          logs::log(logs::debug, "[ENET] Rejecting connection without a fresh control handshake");
          enet_peer_disconnect_now(event.peer, 0);
          continue;
        }

        auto updated = *connections;
        for (const auto &[peer, old] : *connections) {
          if (peer != event.peer && old->session_id == session->session_id) {
            enet_peer_disconnect_now(peer, 0);
            updated = updated.erase(peer);
          }
        }
        connected_clients->store(updated.set(event.peer, immer::box<StreamSession>{*session}));
        logs::log(logs::debug, "[ENET] connected client: {}:{}", client_ip, client_port);
        event_bus->fire_event(immer::box<ResumeStreamEvent>(ResumeStreamEvent{.session_id = session->session_id}));
        continue;
      }

      const auto binding = connections->find(event.peer);
      if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
        connected_clients->store(connections->erase(event.peer));
        if (binding && is_current_connection(running_sessions, **binding)) {
          logs::log(logs::debug, "[ENET] disconnected client: {}:{}", client_ip, client_port);
          event_bus->fire_event(immer::box<PauseStreamEvent>(PauseStreamEvent{.session_id = (*binding)->session_id}));
        }
        continue;
      }

      if (event.type == ENET_EVENT_TYPE_RECEIVE) {
        enet_packet packet = {event.packet, enet_packet_destroy};
        if (!binding || !is_current_connection(running_sessions, **binding)) {
          continue;
        }
        auto client_session = **binding;

        if (packet->dataLength < sizeof(ControlPacket)) {
          logs::log(logs::warning, "[ENET] Dropping {} byte packet, too short for a header", packet->dataLength);
          continue;
        }

        auto type = ((ControlPacket *)packet->data)->type;

        logs::log(logs::trace,
                  "[ENET] received {} of {} bytes from: {}:{} HEX: {}",
                  packet_type_to_str(type),
                  packet->dataLength,
                  client_ip,
                  client_port,
                  crypto::str_to_hex({(char *)packet->data, packet->dataLength}));

        if (type == ENCRYPTED) {
          try {
            auto enc_pkt = (ControlEncryptedPacket *)(packet->data);
            if (!enc_pkt->fits_in(packet->dataLength)) {
              logs::log(logs::warning,
                        "[ENET] Dropping {} byte encrypted packet, its header claims {} bytes",
                        packet->dataLength,
                        enc_pkt->full_size());
              continue;
            }
            const auto mode = client_session.control_encryption->mode();
            auto decrypted = decrypt_packet(*enc_pkt, client_session.aes_key, mode);
            auto sub_type = ((ControlPacket *)decrypted.data())->type;

            logs::log(logs::trace,
                      "[ENET] decrypted sub_type: {} HEX: {}",
                      packet_type_to_str(sub_type),
                      crypto::str_to_hex(decrypted));

            if (sub_type == TERMINATION) {
              event_bus->fire_event(
                  immer::box<PauseStreamEvent>(PauseStreamEvent{.session_id = client_session.session_id}));
            } else if (sub_type == INPUT_DATA) {
              handle_input(
                  client_session,
                  [queue = std::weak_ptr{outgoing},
                   session_id = client_session.session_id,
                   joypads = std::weak_ptr{client_session.joypads}](std::string payload) {
                    const auto pending = queue.lock();
                    auto active_joypads = joypads.lock();
                    if (pending && active_joypads) {
                      pending->try_push({session_id, std::move(payload), std::move(active_joypads)}, queue_capacity);
                    }
                  },
                  (INPUT_PKT *)decrypted.data());
            } else if (sub_type == IDR_FRAME) {
              auto ev = IDRRequestEvent{.session_id = client_session.session_id};
              event_bus->fire_event(immer::box<IDRRequestEvent>{ev});
            }
          } catch (std::runtime_error &e) {
            logs::log(logs::warning, "[ENET] Unable to decrypt incoming packet: {}", e.what());
          }
        } else {
          logs::log(logs::warning,
                    "[ENET] Received unencrypted message: {} - {}",
                    packet_type_to_str(type),
                    crypto::str_to_hex({(char *)packet->data, packet->dataLength}));
        }
      }
    }
  }

  stop_ev.unregister();
}

} // namespace control