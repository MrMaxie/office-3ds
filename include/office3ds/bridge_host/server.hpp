#pragma once

#include "office3ds/bridge/pairing_session.hpp"
#include "office3ds/core/credential_bundle.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace office3ds::bridge_host {

struct ClaimServerConfig {
  core::CredentialBundle credential;
  std::string bind_address{"127.0.0.1"};
  std::uint16_t port = 0;
  std::string product_display_name{"Office 3DS"};
  std::string accent_color{"#2A6F97"};
};

class CredentialClaimServer {
public:
  explicit CredentialClaimServer(ClaimServerConfig config);
  ~CredentialClaimServer();

  CredentialClaimServer(const CredentialClaimServer &) = delete;
  CredentialClaimServer &operator=(const CredentialClaimServer &) = delete;

  [[nodiscard]] std::uint16_t bind();
  [[nodiscard]] std::optional<bridge::PairingOffer>
  begin_pairing(std::string advertised_host, std::chrono::seconds lifetime,
                std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
  [[nodiscard]] bool listen();
  void waitUntilReady() const;
  void stop();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace office3ds::bridge_host
