#pragma once

#include <functional>
#include <optional>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string>
#include <utility.hpp>
#include <variant>

namespace wolf::api {

enum class HTTPMethod {
  GET,
  POST,
  PUT,
  DELETE
};

struct HTTPRequest {
  HTTPMethod method{};
  std::string path{};
  std::string query_string{};
  std::string http_version{};
  SimpleWeb::CaseInsensitiveMultimap headers{};
  std::string body{};
};

struct APIDescription {
  std::string description;
  std::optional<std::string> json_schema;
};

template <typename Socket> struct RequestHandler {
  std::string summary;
  std::string description;
  std::optional<APIDescription> request_description = std::nullopt;
  std::vector<std::pair<int /*status code*/, APIDescription>> response_description = {};
  std::function<void(const HTTPRequest &, Socket &socket)> handler;
};

template <typename T> class HTTPServer {
public:
  HTTPServer(){};

  inline void add(const HTTPMethod &method, const std::string &path, const RequestHandler<T> &handler) {
    endpoints_[{method, path}] = handler;
  }

  inline bool handle_request(const HTTPRequest &request, T &socket) const {
    auto it = endpoints_.find({request.method, request.path});
    if (it != endpoints_.end()) {
      it->second.handler(request, socket);
      return true;
    } else {
      return false;
    }
  }

  std::string openapi_schema() const;

private:
  std::map<std::pair<HTTPMethod, std::string>, RequestHandler<T>> endpoints_ = {};
};

} // namespace wolf::api