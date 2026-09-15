#pragma once

#include "office3ds/core/credential_bundle.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace office3ds::bridge {

struct CredentialClaimRequest {
  std::string pairing_material;
  bool uses_qr_secret = false;
  std::array<std::uint8_t, 32> client_public_key{};
};

struct ProtectedCredentialClaim {
  std::array<std::uint8_t, 32> server_public_key{};
  std::array<std::uint8_t, 24> nonce{};
  std::array<std::uint8_t, 16> authentication_tag{};
  std::string ciphertext;
};

[[nodiscard]] std::optional<std::string>
serialize_credential_claim_request(const CredentialClaimRequest &request);
[[nodiscard]] std::optional<CredentialClaimRequest>
parse_credential_claim_request(std::string_view json);
[[nodiscard]] std::optional<std::string>
serialize_protected_credential_claim(const ProtectedCredentialClaim &claim);
[[nodiscard]] std::optional<ProtectedCredentialClaim>
parse_protected_credential_claim(std::string_view json);
[[nodiscard]] std::optional<ProtectedCredentialClaim>
encrypt_credential_claim(const core::CredentialBundle &credential,
                         const std::array<std::uint8_t, 32> &client_public_key,
                         const std::array<std::uint8_t, 32> &server_secret,
                         const std::array<std::uint8_t, 24> &nonce, std::int64_t now_epoch_seconds);
[[nodiscard]] std::optional<core::CredentialBundle>
decrypt_credential_claim(const ProtectedCredentialClaim &claim,
                         const std::array<std::uint8_t, 32> &client_secret,
                         std::int64_t now_epoch_seconds);

} // namespace office3ds::bridge
