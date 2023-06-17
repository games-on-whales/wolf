#include <boost/json/src.hpp>
#include <curl/curl.h>
#include <docker/docker.hpp>
#include <docker/formatters.hpp>
#include <docker/json_formatters.hpp>
#include <helpers/logger.hpp>
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

void init() {
  curl_global_init(CURL_GLOBAL_ALL);
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

json::value parse(std::string_view json) {
  json::error_code ec;
  auto parsed = json::parse({json.data(), json.size()}, ec);
  if (!ec) {
    return parsed;
  } else {
    logs::log(logs::error, "Error while parsing JSON: {} \n {}", ec.message(), json);
    return json::object(); // Returning an empty object should allow us to continue most of the times
  }
}

std::optional<Container> DockerAPI::get_by_id(std::string_view id) const {
  if (auto conn = docker_connect(socket_path)) {
    auto url = fmt::format("http://localhost/{}/containers/{}/json", DOCKER_API_VERSION, id);
    auto raw_msg = req(conn.value().get(), GET, url);
    if (raw_msg && raw_msg->first == 200) {
      auto json = parse(raw_msg->second);
      return json::value_to<Container>(json);
    } else if (raw_msg) {
      logs::log(logs::warning, "[CURL] error {} - {}", raw_msg->first, raw_msg->second);
    }
  }

  return {};
}

std::vector<Container> DockerAPI::get_containers(bool all) const {
  if (auto conn = docker_connect(socket_path)) {
    auto url = fmt::format("http://localhost/{}/containers/json{}", DOCKER_API_VERSION, all ? "?all=true" : "");
    auto raw_msg = req(conn.value().get(), GET, url);
    if (raw_msg && raw_msg->first == 200) {
      auto json = parse(raw_msg->second);
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
    auto url = fmt::format("http://localhost/{}/containers/create?name={}", DOCKER_API_VERSION, container.name);
    // See: https://stackoverflow.com/a/39149767 and https://github.com/moby/moby/issues/3039
    auto exposed_ports = json::object();
    for (const auto &port : container.ports) {
      exposed_ports[fmt::format("{}/{}", port.public_port, port.type == docker::TCP ? "tcp" : "udp")] = json::object();
    }

    auto post_params = parse(custom_params).as_object();
    post_params["Image"] = container.image;
    merge_array(&post_params, "Env", json::value_from(container.env).as_array());

    if (auto host_cfg = post_params.if_contains("HostConfig")) {
      auto cfg = host_cfg->if_object();
      merge_array(cfg, "Binds", json::value_from(container.mounts).as_array());
      merge_array(cfg, "Devices", json::value_from(container.devices).as_array());
      (*cfg)["PortBindings"] = json::value_from(container.ports);
    } else {
      post_params["HostConfig"] =
          json::object{{"Binds", container.mounts}, {"PortBindings", container.ports}, {"Devices", container.devices}};
    }

    auto json_payload = json::serialize(post_params);
    auto raw_msg = req(conn.value().get(), POST, url, json_payload);
    if (raw_msg && raw_msg->first == 201) {
      auto json = parse(raw_msg->second);
      auto created_id = json.at("Id").as_string();
      return get_by_id(std::string_view{created_id.data(), created_id.size()});
    } else if (raw_msg && raw_msg->first == 404) {      // 404 returned when the image is not present
      logs::log(logs::warning, "[DOCKER] Image {} not present, downloading...", container.image);
      if (pull_image(container.image, registry_auth)) { // Download the image
        return create(container, custom_params, registry_auth,
                      force_recreate_if_present);       // Then retry creating
      } else if (raw_msg) {
        logs::log(logs::warning, "[DOCKER] error {} - {}", raw_msg->first, raw_msg->second);
      }
    } else if (raw_msg && force_recreate_if_present && raw_msg->first == 409) { // 409 returned when there's a conflict
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
        req(conn.value().get(), POST, fmt::format("http://localhost/{}/containers/{}/start", DOCKER_API_VERSION, id));
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
        fmt::format("http://localhost/{}/containers/{}/stop?t={}", DOCKER_API_VERSION, id, timeout_seconds));
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
                               DOCKER_API_VERSION,
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
  if (auto conn = docker_connect(socket_path)) {
    auto api_url = fmt::format("http://localhost/{}/images/create?fromImage={}", DOCKER_API_VERSION, image_name);
    std::vector<std::string> headers = {};
    if (!registry_auth.empty()) {
      headers.push_back(fmt::format("X-Registry-Auth: {}", registry_auth));
    }
    auto raw_msg = req(conn.value().get(), POST, api_url, {}, headers);
    if (raw_msg && raw_msg->first == 200) {
      return true;
    } else if (raw_msg) {
      logs::log(logs::warning, "[DOCKER] error {} - {}", raw_msg->first, raw_msg->second);
      logs::log(logs::info,
                "If it's an authentication error, you can try adding the env variable DOCKER_AUTH_B64, see: "
                "https://docs.docker.com/engine/api/v1.30/#section/Authentication");
    }
  }

  return false;
}

std::string
DockerAPI::get_logs(std::string_view id, bool get_stdout, bool get_stderr, int since, int until, bool timestamps) {
  if (auto conn = docker_connect(socket_path)) {
    auto api_url = fmt::format(
        "http://localhost/{}/containers/{}/logs?stdout={}&stderr={}&since={}&until={}&timestamps={}&follow=false",
        DOCKER_API_VERSION,
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

} // namespace docker