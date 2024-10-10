#pragma once

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <crypto/crypto.hpp>
#include <events/events.hpp>
#include <helpers/logger.hpp>
#include <runners/child_session.hpp>
#include <runners/docker.hpp>
#include <runners/process.hpp>
#include <state/data-structures.hpp>

namespace state {

using namespace wolf::core;
using namespace wolf::config;

/**
 * @brief Will load a configuration from the given source.
 *
 * If the source is not present, it'll provide some sensible defaults
 */
Config load_or_default(const std::string &source,
                       const std::shared_ptr<events::EventBusType> &ev_bus,
                       state::SessionsAtoms running_sessions);

/**
 * Side effect, will atomically update the paired clients list in cfg
 */
void pair(const Config &cfg, const PairedClient &client);

/**
 * Side effect, will atomically remove the client from the list of paired clients
 */
void unpair(const Config &cfg, const PairedClient &client);

/**
 * Returns the first PairedClient with the given client_cert
 */
inline std::optional<PairedClient> get_client_via_ssl(const Config &cfg, x509::x509_ptr client_cert) {
  auto paired_clients = cfg.paired_clients->load();
  auto search_result =
      std::find_if(paired_clients->begin(), paired_clients->end(), [&client_cert](const PairedClient &pair_client) {
        auto paired_cert = x509::cert_from_string(pair_client.client_cert);
        auto verification_error = x509::verification_error(paired_cert, client_cert);
        if (verification_error) {
          logs::log(logs::trace, "X509 certificate verification error: {}", verification_error.value());
          return false;
        } else {
          return true;
        }
      });
  if (search_result != paired_clients->end()) {
    return *search_result;
  } else {
    return std::nullopt;
  }
}

/**
 * Returns the first PairedClient with the given client_cert
 */
inline std::optional<PairedClient> get_client_via_ssl(const Config &cfg, const std::string &client_cert) {
  return get_client_via_ssl(cfg, x509::cert_from_string(client_cert));
}

inline std::size_t get_client_id(const PairedClient &current_client) {
  return std::hash<std::string>{}(current_client.client_cert);
}

inline std::optional<PairedClient> get_client_by_id(const Config &cfg, std::size_t client_id) {
  auto paired_clients = cfg.paired_clients->load();
  auto search_result =
      std::find_if(paired_clients->begin(), paired_clients->end(), [client_id](const PairedClient &pair_client) {
        return get_client_id(pair_client) == client_id;
      });
  if (search_result != paired_clients->end()) {
    return *search_result;
  } else {
    return std::nullopt;
  }
}

/**
 * Return the app with the given app_id (if it exists)
 */
inline std::optional<immer::box<events::App>> get_app_by_id(const Config &cfg, std::string_view app_id) {
  auto apps = cfg.apps->load();
  auto search_result =
      std::find_if(apps->begin(), apps->end(), [&app_id](const events::App &app) { return app.base.id == app_id; });

  if (search_result != apps->end())
    return {*search_result};
  else
    return std::nullopt;
}

inline bool file_exist(const std::string &filename) {
  std::fstream fs(filename);
  return fs.good();
}

inline std::string gen_uuid() {
  auto uuid = boost::uuids::random_generator()();
  return boost::lexical_cast<std::string>(uuid);
}

static std::shared_ptr<events::Runner>
get_runner(const rfl::TaggedUnion<"type", AppCMD, AppDocker, AppChildSession> &runner,
           const std::shared_ptr<events::EventBusType> &ev_bus,
           state::SessionsAtoms running_sessions) {
  if (rfl::holds_alternative<AppCMD>(runner.variant())) {
    auto run_cmd = rfl::get<AppCMD>(runner.variant()).run_cmd;
    return std::make_shared<process::RunProcess>(ev_bus, run_cmd);
  } else if (rfl::holds_alternative<AppDocker>(runner.variant())) {
    return std::make_shared<docker::RunDocker>(
        docker::RunDocker::from_cfg(ev_bus, rfl::get<AppDocker>(runner.variant())));
  } else if (rfl::holds_alternative<AppChildSession>(runner.variant())) {
    auto session_id = rfl::get<AppChildSession>(runner.variant()).parent_session_id;
    return std::make_shared<coop::RunChildSession>(std::stoul(session_id), ev_bus, running_sessions);
  } else {
    logs::log(logs::error, "Found runner of unknown type");
    throw std::runtime_error("Unknown runner type");
  }
}

static moonlight::control::pkts::CONTROLLER_TYPE get_controller_type(const ControllerType &ctrl_type) {
  switch (ctrl_type) {
  case ControllerType::XBOX:
    return moonlight::control::pkts::CONTROLLER_TYPE::XBOX;
  case ControllerType::PS:
    return moonlight::control::pkts::CONTROLLER_TYPE::PS;
  case ControllerType::NINTENDO:
    return moonlight::control::pkts::CONTROLLER_TYPE::NINTENDO;
  case ControllerType::AUTO:
    return moonlight::control::pkts::CONTROLLER_TYPE::AUTO;
  }
  return moonlight::control::pkts::CONTROLLER_TYPE::AUTO;
}
} // namespace state