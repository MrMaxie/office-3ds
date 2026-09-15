#include "office3ds/api/direct_api_client.hpp"
#include "office3ds/bridge/credential_claim.hpp"
#include "office3ds/bridge_host/credential_source.hpp"
#include "office3ds/bridge_host/server.hpp"
#include "office3ds/demo/demo_service.hpp"
#include "office3ds/demo/server.hpp"

#include <httplib.h>
#include <monocypher.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace office3ds::generated {
std::unique_ptr<api::Adapter> create_product_adapter();
}

namespace {

class TemporaryDirectory {
public:
  TemporaryDirectory() : path_(make_path()) { std::filesystem::create_directories(path_); }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  static std::filesystem::path make_path() {
    std::random_device random;
    return std::filesystem::temp_directory_path() /
           ("office-3ds-bridge-integration-" + std::to_string(random()));
  }

  std::filesystem::path path_;
};

std::int64_t now_epoch_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

std::string origin(std::uint16_t port) { return "http://127.0.0.1:" + std::to_string(port); }

std::string between(std::string_view value, std::string_view prefix, std::string_view suffix) {
  const auto start = value.find(prefix);
  assert(start != std::string_view::npos);
  const auto content_start = start + prefix.size();
  const auto end = value.find(suffix, content_start);
  assert(end != std::string_view::npos);
  return std::string(value.substr(content_start, end - content_start));
}

class LoopbackTransport final : public office3ds::api::HttpTransport {
public:
  std::optional<office3ds::api::ApiResponse>
  perform(const office3ds::api::HttpRequest &request) override {
    constexpr std::string_view scheme = "http://";
    if (request.url.rfind(scheme, 0) != 0) {
      return std::nullopt;
    }
    const auto path_start = request.url.find('/', scheme.size());
    if (path_start == std::string::npos) {
      return std::nullopt;
    }
    httplib::Client client(request.url.substr(0, path_start));
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(5, 0);
    client.set_write_timeout(5, 0);
    httplib::Request outgoing;
    outgoing.method = request.method;
    outgoing.path = request.url.substr(path_start);
    outgoing.body = request.body;
    for (const auto &[name, value] : request.headers) {
      outgoing.headers.emplace(name, value);
    }
    const auto result = client.send(outgoing);
    if (!result || result->body.size() > request.max_response_size) {
      return std::nullopt;
    }
    return office3ds::api::ApiResponse{static_cast<std::uint16_t>(result->status), result->body};
  }
};

office3ds::core::CredentialBundle
claim_credential(office3ds::bridge_host::CredentialClaimServer &server,
                 const office3ds::bridge::PairingOffer &offer) {
  std::array<std::uint8_t, 32> client_secret{};
  for (std::size_t index = 0; index < client_secret.size(); ++index) {
    client_secret[index] = static_cast<std::uint8_t>(index + 1U);
  }
  std::array<std::uint8_t, 32> client_public_key{};
  crypto_x25519_public_key(client_public_key.data(), client_secret.data());
  const auto request = office3ds::bridge::serialize_credential_claim_request(
    {offer.pairing_code, false, client_public_key});
  assert(request.has_value());

  httplib::Client client(origin(server.bind()));
  const auto response = client.Post("/v2/claim", *request, "application/json");
  assert(response && response->status == 200);
  const auto protected_claim = office3ds::bridge::parse_protected_credential_claim(response->body);
  assert(protected_claim.has_value());
  const auto credential = office3ds::bridge::decrypt_credential_claim(
    *protected_claim, client_secret, now_epoch_seconds());
  crypto_wipe(client_secret.data(), client_secret.size());
  assert(credential.has_value());

  const auto replay = client.Post("/v2/claim", *request, "application/json");
  assert(replay && replay->status == 401);
  return *credential;
}

void exercise_dashboard_and_recognition(const std::string &api_origin,
                                        const office3ds::core::CredentialBundle &credential) {
  LoopbackTransport transport;
  office3ds::api::DirectApiClient client(transport, api_origin, credential.access_token);
  auto adapter = office3ds::generated::create_product_adapter();
  assert(adapter);
  auto load = adapter->begin_dashboard_load("2026-06-17");
  office3ds::api::DashboardLoadUpdate update;
  for (int operation = 0; operation < 4; ++operation) {
    update = load->advance(client.executor());
  }
  assert(update.finished && update.result == office3ds::api::AdapterResult::ok);
  assert(update.snapshot && update.snapshot->activity.size() == 2U);
  const auto &event = update.snapshot->activity.front();
  const auto recognition = adapter->send_recognition(
    {event.id, event.recipient_id, 5, "Thank you for the review.", "bridge-e2e-001"},
    client.executor());
  assert(recognition.result == office3ds::api::AdapterResult::ok && recognition.accepted);
}

void verify_recognition_after_restart(const std::string &api_origin,
                                      const office3ds::core::CredentialBundle &credential) {
  LoopbackTransport transport;
  office3ds::api::DirectApiClient client(transport, api_origin, credential.access_token);
  office3ds::api::ApiRequest request{"GET", "/v1/recognitions", {}, {}, {}, 64U * 1024U};
  const auto response = client.execute(request);
  assert(response && response->status == 200);
  const auto items = nlohmann::json::parse(response->body).at("items");
  assert(items.size() == 1U && items.at(0).at("request_id") == "bridge-e2e-001");
}

} // namespace

int main() {
  TemporaryDirectory temporary;
  const auto database = temporary.path() / "demo.sqlite3";
  const auto credential_file = temporary.path() / "credential.json";

  office3ds::demo::DemoService issuer({database.string()});
  issuer.issueBearerToken(credential_file.string(), std::chrono::hours(1));
  auto source_credential = office3ds::bridge_host::load_credential_file(
    std::filesystem::absolute(credential_file), "token_expiry", now_epoch_seconds());
  assert(source_credential.has_value());
  assert(!office3ds::bridge_host::load_credential_file(credential_file.filename(), "token_expiry",
                                                       now_epoch_seconds())
            .has_value());
  assert(!office3ds::bridge_host::load_credential_file(std::filesystem::absolute(credential_file),
                                                       "unknown", now_epoch_seconds())
            .has_value());

  office3ds::bridge_host::CredentialClaimServer claim_server(
    {std::move(*source_credential), "127.0.0.1", 0});
  const auto offer = claim_server.begin_pairing("127.0.0.1", std::chrono::minutes(5));
  assert(offer.has_value());
  std::thread claim_thread([&] { assert(claim_server.listen()); });
  claim_server.waitUntilReady();
  httplib::Client claim_status_client(origin(claim_server.bind()));
  const auto active_page = claim_status_client.Get("/");
  assert(active_page && active_page->status == 200);
  assert(active_page->body.find("Time remaining") != std::string::npos);
  assert(active_page->body.find("Generate new pairing code") != std::string::npos);

  const auto first_claimed_credential = claim_credential(claim_server, *offer);
  const auto claimed_page = claim_status_client.Get("/");
  assert(claimed_page && claimed_page->status == 200);
  assert(claimed_page->body.find("Pairing completed") != std::string::npos);
  const auto reset_token = between(claimed_page->body, "name=\"csrf_token\" value=\"", "\"");
  const auto rejected_reset =
    claim_status_client.Post("/reset", "csrf_token=invalid", "application/x-www-form-urlencoded");
  assert(rejected_reset && rejected_reset->status == 403);
  const auto reset = claim_status_client.Post("/reset", "csrf_token=" + reset_token,
                                              "application/x-www-form-urlencoded");
  assert(reset && reset->status == 303);

  const auto renewed_page = claim_status_client.Get("/");
  assert(renewed_page && renewed_page->status == 200);
  assert(renewed_page->body.find("Ready for one device") != std::string::npos);
  office3ds::bridge::PairingOffer renewed_offer;
  renewed_offer.pairing_code =
    between(renewed_page->body, "<span class=\"value code\">", "</span>");
  assert(renewed_offer.pairing_code != offer->pairing_code);
  const auto claimed_credential = claim_credential(claim_server, renewed_offer);
  assert(claimed_credential.access_token == first_claimed_credential.access_token);
  claim_server.stop();
  claim_thread.join();

  {
    office3ds::demo::DemoHttpServer server({database.string(), "127.0.0.1", 0});
    const auto port = server.bind();
    assert(port != 0U);
    std::thread server_thread([&] { assert(server.listen()); });
    server.waitUntilReady();
    httplib::Client unauthorized_client(origin(port));
    const auto unauthorized = unauthorized_client.Get("/v1/profile");
    assert(unauthorized && unauthorized->status == 401);
    exercise_dashboard_and_recognition(origin(port), claimed_credential);
    server.stop();
    server_thread.join();
  }

  {
    office3ds::demo::DemoHttpServer server({database.string(), "127.0.0.1", 0});
    const auto port = server.bind();
    assert(port != 0U);
    std::thread server_thread([&] { assert(server.listen()); });
    server.waitUntilReady();
    verify_recognition_after_restart(origin(port), claimed_credential);
    server.stop();
    server_thread.join();
  }
  return 0;
}
