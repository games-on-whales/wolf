#pragma once

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <functional>
#include <helpers/logger.hpp>
#include <immer/box.hpp>
#include <optional>
#include <rfl.hpp>
#include <string>
#include <utility.hpp>

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
  std::function<void(const HTTPRequest &, Socket socket)> handler;
};

template <typename T> class HTTPServer {
public:
  // Called with the request socket when a handler throws, so the client gets
  // an error response instead of hanging until timeout. May be empty.
  std::function<void(const T &socket, const std::string &error)> on_unhandled_error;

  HTTPServer() : pool_(std::max(std::thread::hardware_concurrency(), 2u)) {};

  ~HTTPServer() {
    pool_.join();
  }

  void add(const HTTPMethod &method, const std::string &path, const RequestHandler<T> &handler) {
    endpoints_[{method, path}] = handler;
  }

  bool handle_request(const HTTPRequest &request, const T &socket) {
    auto it = endpoints_.find({request.method, request.path});
    if (it != endpoints_.end()) {
      auto boxed_request = immer::box<HTTPRequest>(request);
      auto handler = it->second.handler;
      auto error_handler = on_unhandled_error;
      boost::asio::post(pool_, [handler, boxed_request, socket, error_handler]() {
        // An exception escaping a pool task would terminate Wolf
        auto notify = [&](const std::string &error) {
          if (error_handler) {
            try {
              error_handler(socket, error);
            } catch (...) {
              logs::log(logs::error, "[API] Failed to report the unhandled error in {}", boxed_request->path);
            }
          }
        };
        try {
          handler(*boxed_request, socket);
        } catch (const std::exception &e) {
          logs::log(logs::error, "[API] Unhandled error in {}: {}", boxed_request->path, e.what());
          notify(e.what());
        } catch (...) {
          logs::log(logs::error, "[API] Unhandled unknown error in {}", boxed_request->path);
          notify("Unknown error");
        }
      });
      return true;
    }
    return false;
  }

  std::string openapi_schema() const;

private:
  std::map<std::pair<HTTPMethod, std::string>, RequestHandler<T>> endpoints_ = {};
  boost::asio::thread_pool pool_;
};

} // namespace wolf::api