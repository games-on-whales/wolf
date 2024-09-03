#include "core/input.hpp"
#include <control/control.hpp>
#include <control/input_handler.hpp>
#include <events/events.hpp>
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

bool encrypt_and_send(std::string_view payload,
                      std::string_view aes_key,
                      const immer::atom<enet_clients_map> &connected_clients,
                      std::size_t session_id) {
  auto clients = connected_clients.load();
  auto enet_peer = clients->find(session_id);
  if (enet_peer == nullptr) {
    logs::log(logs::debug, "[ENET] Unable to find enet client {}", session_id);
    return false;
  } else {
    auto encrypted = control::encrypt_packet(aes_key, 0, payload); // TODO: seq?
    return send_packet({(char *)encrypted.get(), encrypted->full_size()}, enet_peer->get().get());
  }
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

  immer::atom<enet_clients_map> connected_clients;

  auto stop_ev = event_bus->register_handler<immer::box<StopStreamEvent>>(
      [&connected_clients, &running_sessions](const immer::box<StopStreamEvent> &ev) {
        auto client_session = get_session_by_id(running_sessions->load(), ev->session_id);
        auto terminate_pkt = ControlTerminatePacket{};
        std::string plaintext = {(char *)&terminate_pkt, sizeof(terminate_pkt)};
        encrypt_and_send(plaintext, client_session->aes_key, connected_clients, ev->session_id);
      });

  while (true) {
    if (enet_host_service(host.get(), &event, timeout.count()) > 0) {
      auto [client_ip, client_port] = get_ip((sockaddr *)&event.peer->address.address);
      auto client_session = get_session_by_ip(running_sessions->load(), client_ip);
      if (client_session) {
        switch (event.type) {
        case ENET_EVENT_TYPE_NONE:
          break;
        case ENET_EVENT_TYPE_CONNECT:
          logs::log(logs::debug, "[ENET] connected client: {}:{}", client_ip, client_port);
          connected_clients.update([&event, sess_id = client_session->session_id](const enet_clients_map &m) {
            return m.set(sess_id, std::shared_ptr<ENetPeer>(event.peer, [](auto peer) {
                           // DO NOTHING, we don't want to free peer, the lifecycle is dictated by enet
                         }));
          });
          event_bus->fire_event(
              immer::box<ResumeStreamEvent>(ResumeStreamEvent{.session_id = client_session->session_id}));
          break;
        case ENET_EVENT_TYPE_DISCONNECT:
          logs::log(logs::debug, "[ENET] disconnected client: {}:{}", client_ip, client_port);
          connected_clients.update(
              [sess_id = client_session->session_id](const enet_clients_map &m) { return m.erase(sess_id); });
          event_bus->fire_event(
              immer::box<PauseStreamEvent>(PauseStreamEvent{.session_id = client_session->session_id}));
          break;
        case ENET_EVENT_TYPE_RECEIVE:
          enet_packet packet = {event.packet, enet_packet_destroy};

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
              auto decrypted = decrypt_packet(*enc_pkt, client_session->aes_key);
              auto sub_type = ((ControlPacket *)decrypted.data())->type;

              logs::log(logs::trace,
                        "[ENET] decrypted sub_type: {} HEX: {}",
                        packet_type_to_str(sub_type),
                        crypto::str_to_hex(decrypted));

              if (sub_type == TERMINATION) {
                event_bus->fire_event(
                    immer::box<PauseStreamEvent>(PauseStreamEvent{.session_id = client_session->session_id}));
              } else if (sub_type == INPUT_DATA) {
                handle_input(client_session.value(), connected_clients, (INPUT_PKT *)decrypted.data());
              } else {
                auto ev =
                    ControlEvent{.session_id = client_session->session_id, .type = sub_type, .raw_packet = decrypted};
                event_bus->fire_event(immer::box<ControlEvent>{ev});
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
          break;
        }
      } else {
        logs::log(logs::warning, "[ENET] Received packet from unrecognised client {}:{}", client_ip, client_port);
        enet_peer_disconnect_now(event.peer, 0);
      }
    }
  }

  stop_ev.unregister();
}

} // namespace control