#include <algorithm>
#include <cctype>
#include <core/docker.hpp>
#include <curl/curl.h>
#include <docker/formatters.hpp>
#include <docker/json_formatters.hpp>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <range/v3/view.hpp>
#include <string_view>

namespace wolf::core::docker {
using namespace ranges;
using namespace utils;
namespace json = boost::json;

enum METHOD : int {
  GET,
  POST,
  DELETE
};

void init() {
  curl_global_init(CURL_GLOBAL_ALL);
}

namespace {

std::string lower_copy(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lowered;
}

bool has_name_conflict_text(std::string_view value) {
  const auto lowered = lower_copy(value);
  return lowered.find("already in use") != std::string::npos && lowered.find("name") != std::string::npos;
}

} // namespace

bool is_container_name_conflict_response(long status_code, std::string_view response_body) {
  if (status_code == 409) {
    return true;
  }

  if (status_code != 500) {
    return false;
  }

  boost::system::error_code ec;
  const auto json = json::parse({response_body.data(), response_body.size()}, ec);
  if (!ec) {
    if (const auto obj = json.if_object()) {
      for (const auto field : {"message", "cause"}) {
        if (const auto value = obj->if_contains(field);
            value && value->is_string() && has_name_conflict_text(value->as_string())) {
          return true;
        }
      }
    }
  }

  return has_name_conflict_text(response_body);
}

using curl_ptr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

/**
 * Initialise the curl handle and connects it to the docker socket
 */
std::optional<curl_ptr> docker_connect(const std::string &socket_path, bool debug = false) {
  if (auto curl = curl_easy_init()) {
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str()); // TODO: support also tcp://
    if (debug)
      curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    return curl_ptr(curl, ::curl_easy_cleanup);
  } else {
    return {};
  }
}

/**
 * Perform a HTTP request using curl
 */
std::optional<std::pair<long /* response_code */, std::string /* raw message */>>
req(CURL *handle,
    METHOD method,
    std::string_view target,
    std::string_view post_body = {},
    const std::vector<std::string> &header_params = {}) {
  logs::log(logs::trace, "[CURL] Sending [{}] -> {}", (int)method, target);
  curl_easy_setopt(handle, CURLOPT_URL, target.data());

  /* Set method */
  switch (method) {
  case GET:
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "GET");
    break;
  case POST:
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "POST");
    break;
  case DELETE:
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    break;
  }
  curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

  struct curl_slist *headers = nullptr;
  for (const auto &header : header_params) {
    headers = curl_slist_append(headers, header.c_str());
  }

  /* Pass POST params (if present) */
  if (method == POST && !post_body.empty()) {
    logs::log(logs::trace, "[CURL] POST: {}", post_body);

    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    headers = curl_slist_append(headers, "Transfer-Encoding: chunked");
    headers = curl_slist_append(headers, "Content-type: application/json");
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, post_body.data());
  }
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);

  /* Set custom writer (in order to receive back the response) */
  curl_easy_setopt(
      handle,
      CURLOPT_WRITEFUNCTION,
      static_cast<size_t (*)(char *, size_t, size_t, void *)>([](char *ptr, size_t size, size_t nmemb, void *read_buf) {
        *(static_cast<std::string *>(read_buf)) += std::string{ptr, size * nmemb};
        return size * nmemb;
      }));
  std::string read_buf;
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &read_buf);

  /* Run! */
  auto res = curl_easy_perform(handle);
  curl_slist_free_all(headers);
  if (res != CURLE_OK) {
    logs::log(logs::warning, "[CURL] Request failed with error: {}", curl_easy_strerror(res));
    return {};
  } else {
    long response_code;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
    logs::log(logs::trace, "[CURL] Received {} - {}", response_code, read_buf);
    return {{response_code, read_buf}};
  }
}

std::optional<Container> DockerAPI::get_by_id(std::string_view id) const {
  if (auto conn = docker_connect(socket_path)) {
    auto url = fmt::format("http://localhost/{}/containers/{}/json", docker_api_version, id);
    auto raw_msg = req(conn.value().get(), GET, url);
    if (raw_msg && raw_msg->first == 200) {
      auto json = parse_json(raw_msg->second);
      return json::value_to<Container>(json);
    } else if (raw_msg) {
      logs::log(logs::warning, "[CURL] error {} - {}", raw_msg->first, raw_msg->second);
    }
  }

  return {};
}

std::vector<Container> DockerAPI::get_containers(bool all) const {
  if (auto conn = docker_connect(socket_path)) {
    auto url = fmt::format("http://localhost/{}/containers/json{}", docker_api_version, all ? "?all=true" : "");
    auto raw_msg = req(conn.value().get(), GET, url);
    if (raw_msg && raw_msg->first == 200) {
      auto json = parse_json(raw_msg->second);
      auto containers = json::value_to<std::vector<json::value>>(json);
      return containers                                                            //
             | ranges::views::transform([this](const json::value &container) {     //
                 auto id = container.at("Id").as_string();                         //
                 return get_by_id(std::string_view{id.data(), id.size()}).value(); //
               })                                                                  //
             | ranges::to_vector;                                                  //
    } else if (raw_msg) {
      logs::log(logs::warning, "[CURL] error {} - {}", raw_msg->first, raw_msg->second);
    }
  }

  return {};
}

void merge_array(json::object *root, const std::string &key, const json::array &vec) {
  if (auto current_obj = root->if_contains(key)) {
    auto current = current_obj->if_array();
    std::copy(vec.begin(), vec.end(), std::back_inserter(*current));
    (*root)[key] = *current;
  } else {
    (*root)[key] = vec;
  }
}

std::optional<Container> DockerAPI::create(const Container &container,
                                           std::string_view custom_params,
                                           std::string_view registry_auth,
                                           bool force_recreate_if_present) const {
  if (auto conn = docker_connect(socket_path)) {
    auto url = fmt::format("http://localhost/{}/containers/create?name={}", docker_api_version, container.name);
    // See: https://stackoverflow.com/a/39149767 and https://github.com/moby/moby/issues/3039
    auto exposed_ports = json::object();
    for (const auto &port : container.ports) {
      exposed_ports[fmt::format("{}/{}", port.public_port, port.type == docker::TCP ? "tcp" : "udp")] = json::object();
    }

    auto post_params = parse_json(custom_params).as_object();
    post_params["Image"] = container.image;
    post_params["ExposedPorts"] = exposed_ports;
    merge_array(&post_params, "Env", json::value_from(container.env).as_array());

    if (auto host_cfg = post_params.if_contains("HostConfig")) {
      auto cfg = host_cfg->if_object();
      merge_array(cfg, "Binds", json::value_from(container.mounts).as_array());
      merge_array(cfg, "Devices", json::value_from(container.devices).as_array());
      (*cfg)["PortBindings"] = json::value_from(container.ports);
    } else {
      post_params["HostConfig"] = json::object{{"Binds", json::value_from(container.mounts)},
                                               {"PortBindings", json::value_from(container.ports)},
                                               {"Devices", json::value_from(container.devices)}};
    }

    auto json_payload = json::serialize(post_params);
    auto raw_msg = req(conn.value().get(), POST, url, json_payload);
    if (raw_msg && raw_msg->first == 201) {
      auto json = parse_json(raw_msg->second);
      auto created_id = json.at("Id").as_string();
      return get_by_id(std::string_view{created_id.data(), created_id.size()});
    } else if (raw_msg && raw_msg->first == 404) { // 404 returned when the image is not present
      logs::log(logs::warning, "[DOCKER] Image {} not present, downloading...", container.image);
      if (pull_image(container.image, registry_auth)) { // Download the image
        return create(container, custom_params, registry_auth,
                      force_recreate_if_present); // Then retry creating
      } else if (raw_msg) {
        logs::log(logs::warning, "[DOCKER] error {} - {}", raw_msg->first, raw_msg->second);
      }
    } else if (raw_msg && force_recreate_if_present &&
               is_container_name_conflict_response(raw_msg->first, raw_msg->second)) {
      logs::log(logs::warning, "[DOCKER] Container {} already present, removing first", container.name);
      if (remove_by_name(container.name, true, true, false)) {
        return create(container, custom_params, registry_auth, force_recreate_if_present);
      }
    } else if (raw_msg) {
      logs::log(logs::warning, "[DOCKER] error {} - {}", raw_msg->first, raw_msg->second);
    }
  }

  return {};
}

bool DockerAPI::start_by_id(std::string_view id) const {
  if (auto conn = docker_connect(socket_path)) {
    auto raw_msg =
        req(conn.value().get(), POST, fmt::format("http://localhost/{}/containers/{}/start", docker_api_version, id));
    if (raw_msg && (raw_msg->first == 204 || raw_msg->first == 304)) {
      return true;
    } else if (raw_msg) {
      logs::log(logs::warning, "[DOCKER] error {} - {}", raw_msg->first, raw_msg->second);
    }
  }

  return false;
}

bool DockerAPI::stop_by_id(std::string_view id, int timeout_seconds) const {
  if (auto conn = docker_connect(socket_path)) {
    auto raw_msg = req(
        conn.value().get(),
        POST,
        fmt::format("http://localhost/{}/containers/{}/stop?t={}", docker_api_version, id, timeout_seconds));
    if (raw_msg && (raw_msg->first == 204 || raw_msg->first == 304)) {
      return true;
    } else if (raw_msg) {
      logs::log(logs::warning, "[DOCKER] error {} - {}", raw_msg->first, raw_msg->second);
    }
  }

  return false;
}

bool DockerAPI::remove_by_id(std::string_view id, bool remove_volumes, bool force, bool link) const {
  if (auto conn = docker_connect(socket_path)) {
    auto api_url = fmt::format("http://localhost/{}/containers/{}?v={}&force={}&link={}",
                               docker_api_version,
                               id,
                               remove_volumes,
                               force,
                               link);
    auto raw_msg = req(conn.value().get(), DELETE, api_url);
    if (raw_msg && raw_msg->first == 204) {
      return true;
    } else if (raw_msg) {
      logs::log(logs::warning, "[DOCKER] error {} - {}", raw_msg->first, raw_msg->second);
    }
  }

  return false;
}

bool DockerAPI::remove_by_name(std::string_view name, bool remove_volumes, bool force, bool link) const {
  auto containers = get_containers(true);
  auto container = std::find_if(containers.begin(), containers.end(), [name](Container &container) {
    return container.name == name || container.name == fmt::format("/{}", name);
  });
  if (container != containers.end()) {
    return remove_by_id(container->id, remove_volumes, force, link);
  } else {
    logs::log(logs::warning, "Unable to find container named: {}", name);
  }

  return false;
}

bool DockerAPI::pull_image(std::string_view image_name, std::string_view registry_auth) const {
  return pull_image(image_name, registry_auth, [image_name](const DockerProgressEvent &progress) {
    logs::log(logs::debug,
              "[DOCKER] Pulling image {} - {} {}%)",
              image_name,
              progress.layer_id,
              progress.total != 0 ? progress.current_progress * 100 / progress.total : 0);
  });
}

bool DockerAPI::pull_image(std::string_view image_name,
                           std::string_view registry_auth,
                           const std::function<void(const DockerProgressEvent &)> &progress_fn) const {
  if (auto conn = docker_connect(socket_path)) {
    auto api_url = fmt::format("http://localhost/{}/images/create?fromImage={}", docker_api_version, image_name);

    struct PullState {
      std::string buffer = {};
      const std::function<void(const DockerProgressEvent &)> &progress_fn;
      std::optional<std::string> error_msg = std::nullopt;
    };

    PullState state{.progress_fn = progress_fn};

    auto write_callback = +[](char *ptr, size_t size, size_t nmemb, void *userdata) -> size_t {
      auto *pull_state = static_cast<PullState *>(userdata);
      const size_t data_size = size * nmemb;
      pull_state->buffer.append(ptr, data_size);

      size_t pos;
      while ((pos = pull_state->buffer.find('\n')) != std::string::npos) {
        std::string line = pull_state->buffer.substr(0, pos);
        pull_state->buffer.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }

        if (line.empty()) {
          continue;
        }

        try {
          auto data = json::parse(line);
          if (data.is_object()) {
            auto &obj = data.as_object();
            DockerProgressEvent event{};

            if (auto error = obj.find("error"); error != obj.end() && error->value().is_object()) {
              pull_state->error_msg = error->value().as_string().c_str();
            }

            if (auto it = obj.find("id"); it != obj.end() && it->value().is_string()) {
              event.layer_id = it->value().as_string().c_str();
              if (auto it = obj.find("progressDetail"); it != obj.end() && it->value().is_object()) {
                auto &detail = it->value().as_object();
                if (auto cur = detail.find("current"); cur != detail.end() && cur->value().is_int64()) {
                  event.current_progress = cur->value().as_int64();
                }
                if (auto tot = detail.find("total"); tot != detail.end() && tot->value().is_int64()) {
                  event.total = tot->value().as_int64();
                }
                pull_state->progress_fn(event);
              }
            }
          }
        } catch (...) {
          logs::log(logs::warning, "Unable to parse progress event: {}", line);
        }
      }
      return data_size;
    };

    curl_easy_setopt(conn->get(), CURLOPT_URL, api_url.c_str());
    curl_easy_setopt(conn->get(), CURLOPT_POST, 1L);
    curl_easy_setopt(conn->get(), CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(conn->get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(conn->get(), CURLOPT_WRITEDATA, &state);

    struct curl_slist_deleter {
      void operator()(curl_slist *list) const {
        if (list)
          curl_slist_free_all(list);
      }
    };
    std::unique_ptr<curl_slist, curl_slist_deleter> headers;

    if (!registry_auth.empty()) {
      struct curl_slist *slist = nullptr;
      std::string auth_header = "X-Registry-Auth: " + std::string(registry_auth);
      slist = curl_slist_append(slist, auth_header.c_str());
      headers.reset(slist);
      curl_easy_setopt(conn->get(), CURLOPT_HTTPHEADER, headers.get());
    }

    CURLcode res = curl_easy_perform(conn->get());

    if (res != CURLE_OK) {
      return false;
    }

    long response_code = 0;
    curl_easy_getinfo(conn->get(), CURLINFO_RESPONSE_CODE, &response_code);

    if (state.error_msg.has_value()) {
      logs::log(logs::warning, "[DOCKER] error {} - {}", response_code, state.error_msg.value());
      logs::log(logs::info,
                "If it's an authentication error, you can try adding the env variable DOCKER_AUTH_B64, see: "
                "https://docs.docker.com/engine/api/v1.30/#section/Authentication");
      return false;
    } else {
      return response_code == 200;
    }
  }

  return false;
}

/**
 * Returns the full json response as is from /images/{image_name}/json
 * Optional because the image might be missing locally
 */
std::optional<std::string> DockerAPI::inspect_image(std::string_view image_name) const {
  if (auto conn = docker_connect(socket_path)) {
    auto api_url = fmt::format("http://localhost/{}/images/{}/json", docker_api_version, image_name);
    auto raw_msg = req(conn.value().get(), GET, api_url);
    if (raw_msg && raw_msg->first == 200) {
      return raw_msg->second;
    } else if (raw_msg) {
      logs::log(logs::trace, "[DOCKER] inspect_image returned {} - {}", raw_msg->first, raw_msg->second);
    }
  }
  return std::nullopt;
}

std::string
DockerAPI::get_logs(std::string_view id, bool get_stdout, bool get_stderr, int since, int until, bool timestamps) {
  if (auto conn = docker_connect(socket_path)) {
    auto api_url = fmt::format(
        "http://localhost/{}/containers/{}/logs?stdout={}&stderr={}&since={}&until={}&timestamps={}&follow=false",
        docker_api_version,
        id,
        get_stdout,
        get_stderr,
        since,
        until,
        timestamps);
    auto raw_msg = req(conn.value().get(), GET, api_url);
    if (raw_msg && raw_msg->first == 200) {
      return raw_msg->second; // TODO: erase first 8 bytes from each line, see Stream format in the API docs
    } else if (raw_msg) {
      logs::log(logs::warning, "[DOCKER] error {} - {}", raw_msg->first, raw_msg->second);
    }
  }

  return "";
}

bool DockerAPI::exec(std::string_view id, const std::vector<std::string_view> &command, std::string_view user) const {
  if (auto conn = docker_connect(socket_path)) {
    auto api_url = fmt::format("http://localhost/{}/containers/{}/exec", docker_api_version, id);
    auto post_params = json::object{
        {"Cmd", json::value_from(command)},
        {"User", user},
        {"AttachStdin", false},
        {"AttachStdout", true},
        {"AttachStderr", true},
    };
    auto json_payload = json::serialize(post_params);
    auto raw_msg = req(conn.value().get(), POST, api_url, json_payload);
    if (raw_msg && raw_msg->first == 201) {
      // Exec request created, start it
      auto json = parse_json(raw_msg->second);
      std::string exec_id = json.at("Id").as_string().data();
      api_url = fmt::format("http://localhost/{}/exec/{}/start", docker_api_version, exec_id);
      post_params = json::object{{"Detach", false}, {"Tty", false}};
      json_payload = json::serialize(post_params);
      raw_msg = req(conn.value().get(), POST, api_url, json_payload);
      if (raw_msg && raw_msg->first == 200) {
        auto console = raw_msg->second;
        // Exec request completed, inspect the results
        api_url = fmt::format("http://localhost/{}/exec/{}/json", docker_api_version, exec_id);
        raw_msg = req(conn.value().get(), GET, api_url);
        if (raw_msg && raw_msg->first == 200) {
          json = parse_json(raw_msg->second);
          auto exit_code = json.at("ExitCode").as_int64();
          if (exit_code != 0) {
            logs::log(logs::warning, "Docker exec failed ({}), {}", exit_code, console);
            return false;
          } else {
            return true;
          }
        }
      }
    }

    if (raw_msg) {
      logs::log(logs::warning, "[DOCKER] error {} - {}", raw_msg->first, raw_msg->second);
    }
  }

  return false;
}

std::string DockerAPI::get_api_version() {
  if (auto conn = docker_connect(socket_path)) {
    auto raw_msg = req(conn.value().get(), GET, "http://localhost/version");
    if (raw_msg && raw_msg->first == 200) {
      auto json = parse_json(raw_msg->second);
      try {
        auto version = json.at("ApiVersion").as_string().c_str();
        return fmt::format("v{}", version);
      } catch (...) {
        logs::log(logs::warning, "[DOCKER] Unable to retrieve docker API version {}", raw_msg->second);
      }
    }
  }
  logs::log(logs::warning, "[DOCKER] Unable to retrieve docker API version, falling back to default");
  return "v1.40";
}

} // namespace wolf::core::docker
