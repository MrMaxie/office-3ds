#include "office3ds/core/credential_bundle.hpp"

#include <nlohmann/json.hpp>

#include <limits>

namespace office3ds::core {
namespace {

constexpr int kCredentialFormatVersion = 2;
constexpr std::size_t kMaximumAccessTokenLength = 16U * 1024U;
constexpr std::size_t kMaximumCredentialJsonLength = 20U * 1024U;

bool contains_unsafe_character(std::string_view value) {
  for (const unsigned char character : value) {
    if (character <= 0x20U || character == 0x7FU) {
      return true;
    }
  }
  return false;
}

} // namespace

std::optional<std::int64_t>
credential_device_expiry_epoch_seconds(const CredentialBundle &credential) {
  if ((credential.clock_offset_seconds > 0 &&
       credential.expires_at_epoch_seconds <
         std::numeric_limits<std::int64_t>::min() + credential.clock_offset_seconds) ||
      (credential.clock_offset_seconds < 0 &&
       credential.expires_at_epoch_seconds >
         std::numeric_limits<std::int64_t>::max() + credential.clock_offset_seconds)) {
    return std::nullopt;
  }
  return credential.expires_at_epoch_seconds - credential.clock_offset_seconds;
}

bool is_credential_usable(const CredentialBundle &credential, std::int64_t now_epoch_seconds) {
  const auto device_expiry = credential_device_expiry_epoch_seconds(credential);
  return !credential.access_token.empty() &&
         credential.access_token.size() <= kMaximumAccessTokenLength &&
         !contains_unsafe_character(credential.access_token) && device_expiry.has_value() &&
         *device_expiry > now_epoch_seconds;
}

std::optional<CredentialBundle> parse_credential_bundle(std::string_view json,
                                                        std::int64_t now_epoch_seconds) {
  if (json.empty() || json.size() > kMaximumCredentialJsonLength) {
    return std::nullopt;
  }
  try {
    const auto document = nlohmann::json::parse(json);
    if (!document.is_object() || document.size() != 4 ||
        document.at("version").get<int>() != kCredentialFormatVersion) {
      return std::nullopt;
    }
    CredentialBundle credential{document.at("access_token").get<std::string>(),
                                document.at("expires_at").get<std::int64_t>(),
                                document.at("clock_offset_seconds").get<std::int64_t>()};
    return is_credential_usable(credential, now_epoch_seconds)
             ? std::optional<CredentialBundle>{std::move(credential)}
             : std::nullopt;
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
}

std::optional<std::string> serialize_credential_bundle(const CredentialBundle &credential,
                                                       std::int64_t now_epoch_seconds) {
  if (!is_credential_usable(credential, now_epoch_seconds)) {
    return std::nullopt;
  }
  return nlohmann::json{{"version", kCredentialFormatVersion},
                        {"access_token", credential.access_token},
                        {"expires_at", credential.expires_at_epoch_seconds},
                        {"clock_offset_seconds", credential.clock_offset_seconds}}
    .dump();
}

} // namespace office3ds::core
