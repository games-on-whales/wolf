#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <future>
#include <rest/rest.hpp>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    auto base = fs::temp_directory_path() / "wolftests-https-XXXXXX";
    std::string tmpl = base.string();
    if (mkdtemp(tmpl.data()) == nullptr) {
      throw std::runtime_error("mkdtemp failed");
    }
    path = tmpl;
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

size_t discard_cb(char *, size_t size, size_t nmemb, void *) {
  return size * nmemb;
}

} // namespace

/**
 * Regression test: Wolf's HTTPS server must allow TLS session resumption when
 * the client presents a certificate. Without `SSL_CTX_set_session_id_context`
 * being called on the server context, OpenSSL aborts any resumption-capable
 * handshake from a client that sends a cert, with error:
 *   "session id context uninitialized".
 *
 * libcurl (and most clients) enable session resumption by default, so the
 * second request on a kept-alive easy handle (new TCP connection to the same
 * host:port, same SSL_CTX on the server) will attempt resumption and fail if
 * the context id is not set.
 *
 * This test sets up a real HTTPS server on an ephemeral port, then issues two
 * sequential libcurl requests over that handle. The second request triggers
 * TLS session resumption, which must succeed.
 */
TEST_CASE("HTTPS server supports session resumption with client cert", "[HTTPS]") {
  // ── 1. Generate server + client keys/certs and write to a temp dir ──
  TempDir tmp;
  auto server_key_path = (tmp.path / "server.key").string();
  auto server_cert_path = (tmp.path / "server.cert").string();
  auto client_key_path = (tmp.path / "client.key").string();
  auto client_cert_path = (tmp.path / "client.cert").string();

  {
    auto skey = x509::generate_key();
    auto scert = x509::generate_x509(skey);
    REQUIRE(x509::write_to_disk(skey, server_key_path, scert, server_cert_path));
  }
  {
    auto ckey = x509::generate_key();
    auto ccert = x509::generate_x509(ckey);
    REQUIRE(x509::write_to_disk(ckey, client_key_path, ccert, client_cert_path));
  }

  // ── 2. Start Wolf's HTTPS server on an ephemeral port ──
  HttpsServer server(server_cert_path, server_key_path);
  server.config.address = "127.0.0.1";
  server.config.port = 0;

  std::atomic<int> hits{0};
  server.resource["^/ping$"]["GET"] = [&hits](auto resp, auto /*req*/) {
    hits.fetch_add(1);
    resp->write("pong");
  };

  std::promise<unsigned short> port_promise;
  auto port_future = port_promise.get_future();
  std::thread server_thread([&] {
    server.start([&port_promise](unsigned short port) { port_promise.set_value(port); });
  });

  // Wait for bind
  REQUIRE(port_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  auto port = port_future.get();
  REQUIRE(port != 0);

  // ── 3. Drive two TLS handshakes with client cert over a reusable libcurl handle ──
  curl_global_init(CURL_GLOBAL_DEFAULT);
  CURL *curl = curl_easy_init();
  REQUIRE(curl != nullptr);

  auto url = "https://127.0.0.1:" + std::to_string(port) + "/ping";
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_SSLCERT, client_cert_path.c_str());
  curl_easy_setopt(curl, CURLOPT_SSLKEY, client_key_path.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_cb);
  // Force a new TCP connection on the second request so the resumption path
  // is exercised at the TLS layer instead of reusing the keep-alive socket.
  curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
  // Session id cache is on by default; make it explicit.
  curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);

  CAPTURE(url);

  auto rc1 = curl_easy_perform(curl);
  CHECK(rc1 == CURLE_OK);

  auto rc2 = curl_easy_perform(curl);
  CHECK(rc2 == CURLE_OK);

  CHECK(hits.load() == 2);

  curl_easy_cleanup(curl);
  curl_global_cleanup();

  // ── 4. Shut down ──
  server.stop();
  if (server_thread.joinable()) {
    server_thread.join();
  }
}
