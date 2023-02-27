#include <boost/asio.hpp>
#include <curl/curl.h>
#include <docker/docker.hpp>
#include <docker/formatters.hpp>
#include <helpers/logger.hpp>
#include <memory>
#include <range/v3/view.hpp>
#include <string_view>

namespace docker {
using namespace ranges;
namespace json = boost::json;

enum METHOD {
  GET,
  POST,
  DELETE
};

using curl_ptr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

/**
 * Initialise the curl handle and connects it to the docker socket
 */
std::optional<curl_ptr> docker_connect(bool debug = false) {
  if (auto curl = curl_easy_init()) {
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, "/var/run/docker.sock");
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
std::optional<std::string> req(CURL *handle, METHOD method, std::string_view target, std::string_view post_body = {}) {
  logs::log(logs::trace, "[CURL] Sending [{}] -> {}", method, target);
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

  /* Pass POST params (if present) */
  struct curl_slist *headers = nullptr;
  if (method == POST && !post_body.empty()) {
    logs::log(logs::trace, "[CURL] POST: {}", post_body);

    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    headers = curl_slist_append(headers, "Transfer-Encoding: chunked");
    headers = curl_slist_append(headers, "Content-type: application/json");
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, post_body.data());
  }

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
    logs::log(logs::trace, "[CURL] Received msg: {}", read_buf);

    long response_code;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code >= 200 &&
        response_code <= 304) { // 304 is used when things are already in the required state (ex: already stopped)
      return read_buf;
    } else {
      logs::log(logs::warning, "[CURL] error, response code: {}, msg: {}", response_code, read_buf);
      return {};
    }
  }
}

json::value parse(std::string_view json) {
  json::error_code ec;
  return json::parse({json.data(), json.size()}, ec);
}

std::optional<Container> get_by_id(std::string_view id) {
  if (auto conn = docker_connect()) {
    auto url = fmt::format("http://localhost/{}/containers/{}/json", DOCKER_API_VERSION, id);
    auto raw_msg = req(conn.value().get(), GET, url);
    if (raw_msg) {
      auto json = parse(raw_msg.value());
      return json::value_to<Container>(json);
    }
  }

  return {};
}

std::vector<Container> get_containers(bool all) {
  if (auto conn = docker_connect()) {
    auto url = fmt::format("http://localhost/{}/containers/json{}", DOCKER_API_VERSION, all ? "?all=true" : "");
    auto raw_msg = req(conn.value().get(), GET, url);
    if (raw_msg) {
      auto json = parse(raw_msg.value());
      auto containers = json::value_to<std::vector<json::value>>(json);
      return containers                                                            //
             | ranges::views::transform([](const json::value &container) {         //
                 auto id = container.at("Id").as_string();                         //
                 return get_by_id(std::string_view{id.data(), id.size()}).value(); //
               })                                                                  //
             | ranges::to_vector;                                                  //
    }
  }

  return {};
}

std::optional<Container> create(const Container &container) {
  if (auto conn = docker_connect()) {
    auto url = fmt::format("http://localhost/{}/containers/create?name={}", DOCKER_API_VERSION, container.name);
    // See: https://stackoverflow.com/a/39149767 and https://github.com/moby/moby/issues/3039
    auto exposed_ports = json::object();
    for (const auto &port : container.ports) {
      exposed_ports[fmt::format("{}/{}", port.public_port, port.type == docker::TCP ? "tcp" : "udp")] = json::object();
    }
    json::value post_params = {
        {"Image", container.image},
        {"Env", container.env},
        {"ExposedPorts", exposed_ports},
        {"HostConfig", json::object{{"Binds", container.mounts}, {"PortBindings", container.ports}}}};
    auto raw_msg = req(conn.value().get(), POST, url, json::serialize(post_params));
    if (raw_msg) {
      auto json = parse(raw_msg.value());
      auto created_id = json.at("Id").as_string();
      return get_by_id(std::string_view{created_id.data(), created_id.size()});
    }
  }

  return {};
}

bool start_by_id(std::string_view id) {
  if (auto conn = docker_connect()) {
    auto raw_msg =
        req(conn.value().get(), POST, fmt::format("http://localhost/{}/containers/{}/start", DOCKER_API_VERSION, id));
    if (raw_msg) {
      return true;
    }
  }

  return false;
}

bool stop_by_id(std::string_view id, int timeout_seconds) {
  if (auto conn = docker_connect()) {
    auto raw_msg = req(
        conn.value().get(),
        POST,
        fmt::format("http://localhost/{}/containers/{}/stop?t={}", DOCKER_API_VERSION, id, timeout_seconds));
    if (raw_msg) {
      return true;
    }
  }

  return false;
}

bool remove_by_id(std::string_view id, bool remove_volumes, bool force, bool link) {
  if (auto conn = docker_connect()) {
    auto api_url = fmt::format("http://localhost/{}/containers/{}?v={},force={},link={}",
                               DOCKER_API_VERSION,
                               id,
                               remove_volumes,
                               force,
                               link);
    auto raw_msg = req(conn.value().get(), DELETE, api_url);
    if (raw_msg) {
      return true;
    }
  }

  return false;
}

void init() {
  curl_global_init(CURL_GLOBAL_ALL);
}

} // namespace docker