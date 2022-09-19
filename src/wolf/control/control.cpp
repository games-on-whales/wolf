#include <control/packet_utils.hpp>
#include <helpers/logger.hpp>
#include <range/v3/view.hpp>
#include <state/data-structures.hpp>
#include <sys/socket.h>
#include <thread>

namespace control {

using namespace ranges;
using namespace moonlight::control;

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

  auto enet_host = enet_host_create(AF_INET, &addr, peers, 1, 0, 0);
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

std::thread start_service(immer::box<state::ControlSession> control_sess, int timeout_millis = 1000) {
  return std::thread(
      [timeout_millis](immer::box<state::ControlSession> control_sess) {
        enet_host host = create_host(control_sess->host, control_sess->port, control_sess->peers);
        logs::log(logs::info, "Control server started on port: {}", control_sess->port);

        ENetEvent event;
        bool terminated = false;
        while (!terminated && enet_host_service(host.get(), &event, timeout_millis) > 0) {
          auto [client_ip, client_port] = get_ip((sockaddr *)&event.peer->address.address);

          switch (event.type) {
          case ENET_EVENT_TYPE_NONE:
            break;
          case ENET_EVENT_TYPE_CONNECT:
            logs::log(logs::debug, "[ENET] connected client: {}:{}", client_ip, client_port);
            break;
          case ENET_EVENT_TYPE_DISCONNECT:
            logs::log(logs::debug, "[ENET] disconnected client: {}:{}", client_ip, client_port);
            control_sess->event_bus->fire_event(immer::box<ControlEvent>{control_sess->session_id, TERMINATION, ""});
            terminated = true;
            break;
          case ENET_EVENT_TYPE_RECEIVE:
            enet_packet packet = {event.packet, enet_packet_destroy};

            auto type = get_type(packet->data);

            logs::log(logs::trace,
                      "[ENET] received {} of {} bytes from: {}:{} HEX: {}",
                      packet_type_to_str(type),
                      packet->dataLength,
                      client_ip,
                      client_port,
                      crypto::str_to_hex({(char *)packet->data, packet->dataLength}));

            if (type == ENCRYPTED) {
              try {
                auto decrypted = decrypt_packet(packet->data, control_sess->aes_key);

                auto sub_type = get_type(reinterpret_cast<const enet_uint8 *>(decrypted.data()));

                logs::log(logs::trace,
                          "[ENET] decrypted sub_type: {} HEX: {}",
                          packet_type_to_str(sub_type),
                          crypto::str_to_hex(decrypted));

                auto ev = ControlEvent{control_sess->session_id, sub_type, decrypted};
                control_sess->event_bus->fire_event(immer::box<ControlEvent>{ev});
              } catch (std::runtime_error &e) {
                logs::log(logs::error, "[ENET] Unable to decrypt incoming packet: {}", e.what());
              }
            }

            // TODO: read and parse payload
            break;
          }
        }
      },
      std::move(control_sess));
}

} // namespace control