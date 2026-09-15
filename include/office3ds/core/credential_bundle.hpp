#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace office3ds::core {

struct CredentialBundle {
  std::string access_token;
  std::int64_t expires_at_epoch_seconds = 0;
  std::int64_t clock_offset_seconds = 0;
};

[[nodiscard]] std::optional<std::int64_t>
credential_device_expiry_epoch_seconds(const CredentialBundle &credential);
[[nodiscard]] bool is_credential_usable(const CredentialBundle &credential,
                                        std::int64_t now_epoch_seconds);
[[nodiscard]] std::optional<CredentialBundle>
parse_credential_bundle(std::string_view json, std::int64_t now_epoch_seconds);
[[nodiscard]] std::optional<std::string>
serialize_credential_bundle(const CredentialBundle &credential, std::int64_t now_epoch_seconds);

} // namespace office3ds::core
