#include "office3ds/platform_3ds/bridge_claim_source.hpp"

#include "office3ds/bridge/credential_claim.hpp"

#include <array>
#include <cstring>
#include <ctime>
#include <limits>
#include <vector>

#include <3ds.h>
#include <monocypher.h>

namespace office3ds::platform_3ds {
namespace {

constexpr std::size_t kMaximumResponseSize = 48U * 1024U;
constexpr u64 kResponseTimeoutNanoseconds = 10'000'000'000ULL;

void wipe(std::array<std::uint8_t, 32> &bytes) { crypto_wipe(bytes.data(), bytes.size()); }

std::optional<std::pair<u32, std::string>> request_claim(const bridge::PairingEndpoint &endpoint,
                                                         std::string_view body) {
  const auto url = "http://" + endpoint.host + ':' + std::to_string(endpoint.port) + "/v2/claim";
  httpcContext context{};
  if (R_FAILED(httpcOpenContext(&context, HTTPC_METHOD_POST, url.c_str(), 0))) {
    return std::nullopt;
  }
  const auto close = [&context]() { httpcCloseContext(&context); };
  const auto cancel_and_close = [&context]() {
    httpcCancelConnection(&context);
    httpcCloseContext(&context);
  };
  if (body.size() > std::numeric_limits<u32>::max() ||
      R_FAILED(httpcAddRequestHeaderField(&context, "Content-Type", "application/json"))) {
    close();
    return std::nullopt;
  }
  std::vector<u32> post_data((body.size() + sizeof(u32) - 1U) / sizeof(u32), 0U);
  std::memcpy(post_data.data(), body.data(), body.size());
  if (R_FAILED(httpcAddPostDataRaw(&context, post_data.data(), static_cast<u32>(body.size()))) ||
      R_FAILED(httpcBeginRequest(&context))) {
    cancel_and_close();
    return std::nullopt;
  }
  u32 status = 0;
  u32 downloaded_size = 0;
  u32 content_size = 0;
  if (R_FAILED(httpcGetResponseStatusCodeTimeout(&context, &status, kResponseTimeoutNanoseconds)) ||
      R_FAILED(httpcGetDownloadSizeState(&context, &downloaded_size, &content_size)) ||
      content_size > kMaximumResponseSize) {
    cancel_and_close();
    return std::nullopt;
  }
  std::string response(content_size, '\0');
  if (content_size > 0) {
    u32 received_size = 0;
    if (R_FAILED(httpcDownloadData(&context, reinterpret_cast<u8 *>(response.data()), content_size,
                                   &received_size)) ||
        received_size != content_size) {
      cancel_and_close();
      return std::nullopt;
    }
  }
  close();
  return std::pair{status, std::move(response)};
}

} // namespace

BridgeClaimSource::BridgeClaimSource(bridge::PairingEndpoint endpoint, std::string pairing_material,
                                     bool uses_qr_secret)
    : endpoint_(std::move(endpoint)), pairing_material_(std::move(pairing_material)),
      uses_qr_secret_(uses_qr_secret) {}

BridgeClaimSource::~BridgeClaimSource() { cancel(); }

platform::CredentialClaimResult BridgeClaimSource::poll() {
  if (finished_ || pairing_material_.empty() || !bridge::is_local_bridge_ipv4(endpoint_.host) ||
      endpoint_.port == 0) {
    return {platform::CredentialClaimStatus::invalid_response, std::nullopt};
  }
  finished_ = true;
  std::array<std::uint8_t, 32> client_secret{};
  std::array<std::uint8_t, 32> client_public_key{};
  if (R_FAILED(psInit())) {
    return {platform::CredentialClaimStatus::unavailable, std::nullopt};
  }
  const auto random_result = PS_GenerateRandomBytes(client_secret.data(), client_secret.size());
  psExit();
  if (R_FAILED(random_result)) {
    return {platform::CredentialClaimStatus::unavailable, std::nullopt};
  }
  crypto_x25519_public_key(client_public_key.data(), client_secret.data());
  const auto request = bridge::serialize_credential_claim_request(
    {pairing_material_, uses_qr_secret_, client_public_key});
  if (!request.has_value()) {
    wipe(client_secret);
    return {platform::CredentialClaimStatus::invalid_response, std::nullopt};
  }
  const auto response = request_claim(endpoint_, *request);
  if (!response.has_value()) {
    wipe(client_secret);
    return {platform::CredentialClaimStatus::unavailable, std::nullopt};
  }
  if (response->first == 401) {
    wipe(client_secret);
    return {platform::CredentialClaimStatus::rejected, std::nullopt};
  }
  if (response->first != 200) {
    wipe(client_secret);
    return {platform::CredentialClaimStatus::unavailable, std::nullopt};
  }
  const auto protected_claim = bridge::parse_protected_credential_claim(response->second);
  if (!protected_claim.has_value()) {
    wipe(client_secret);
    return {platform::CredentialClaimStatus::invalid_response, std::nullopt};
  }
  const auto device_time = std::time(nullptr);
  auto credential = bridge::decrypt_credential_claim(*protected_claim, client_secret, device_time);
  wipe(client_secret);
  if (!credential.has_value()) {
    return {platform::CredentialClaimStatus::invalid_response, std::nullopt};
  }
  crypto_wipe(pairing_material_.data(), pairing_material_.size());
  pairing_material_.clear();
  endpoint_.secret.clear();
  return {platform::CredentialClaimStatus::accepted, std::move(credential)};
}

void BridgeClaimSource::cancel() noexcept {
  finished_ = true;
  if (!pairing_material_.empty()) {
    crypto_wipe(pairing_material_.data(), pairing_material_.size());
    pairing_material_.clear();
  }
  if (!endpoint_.secret.empty()) {
    crypto_wipe(endpoint_.secret.data(), endpoint_.secret.size());
    endpoint_.secret.clear();
  }
}

} // namespace office3ds::platform_3ds
