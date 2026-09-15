#include "office3ds/bridge_host/credential_source.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>

namespace office3ds::bridge_host {
namespace {

constexpr std::uintmax_t kMaximumCredentialFileSize = 32U * 1024U;

int base64url_value(char value) {
  if (value >= 'A' && value <= 'Z') {
    return value - 'A';
  }
  if (value >= 'a' && value <= 'z') {
    return value - 'a' + 26;
  }
  if (value >= '0' && value <= '9') {
    return value - '0' + 52;
  }
  if (value == '-') {
    return 62;
  }
  if (value == '_') {
    return 63;
  }
  return -1;
}

std::optional<std::string> decode_base64url(std::string_view encoded) {
  if (encoded.empty() || encoded.size() > 16U * 1024U || encoded.size() % 4U == 1U) {
    return std::nullopt;
  }
  std::string decoded;
  decoded.reserve(encoded.size() * 3U / 4U);
  std::uint32_t buffer = 0;
  unsigned bits = 0;
  for (const char character : encoded) {
    const auto value = base64url_value(character);
    if (value < 0) {
      return std::nullopt;
    }
    buffer = (buffer << 6U) | static_cast<std::uint32_t>(value);
    bits += 6U;
    if (bits >= 8U) {
      bits -= 8U;
      decoded.push_back(static_cast<char>((buffer >> bits) & 0xffU));
    }
  }
  if (bits != 0U && (buffer & ((1U << bits) - 1U)) != 0U) {
    return std::nullopt;
  }
  return decoded;
}

bool exact_fields(const nlohmann::json &document, std::initializer_list<std::string_view> fields) {
  if (!document.is_object() || document.size() != fields.size()) {
    return false;
  }
  return std::all_of(fields.begin(), fields.end(),
                     [&](std::string_view field) { return document.contains(std::string(field)); });
}

void secure_wipe(std::string &value) noexcept {
  volatile char *data = value.empty() ? nullptr : value.data();
  for (std::size_t index = 0; data != nullptr && index < value.size(); ++index) {
    data[index] = '\0';
  }
  value.clear();
}

std::optional<core::CredentialBundle> parse_token_expiry(std::string_view contents,
                                                         std::int64_t now_epoch_seconds) {
  try {
    const auto document = nlohmann::json::parse(contents);
    if (!exact_fields(document, {"access_token", "expires_at"}) ||
        !document.at("access_token").is_string() ||
        !document.at("expires_at").is_number_integer()) {
      return std::nullopt;
    }
    core::CredentialBundle credential{document.at("access_token").get<std::string>(),
                                      document.at("expires_at").get<std::int64_t>()};
    if (credential.access_token.empty() || credential.access_token.size() > 16U * 1024U ||
        credential.access_token.find_first_of("\r\n") != std::string::npos ||
        !core::is_credential_usable(credential, now_epoch_seconds)) {
      return std::nullopt;
    }
    return credential;
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
}

std::optional<core::CredentialBundle> parse_jwt(std::string_view token,
                                                std::int64_t now_epoch_seconds) {
  if (token.empty() || token.size() > 16U * 1024U ||
      token.find_first_of("\r\n") != std::string_view::npos) {
    return std::nullopt;
  }
  const auto first_separator = token.find('.');
  const auto second_separator = token.find(
    '.', first_separator == std::string_view::npos ? first_separator : first_separator + 1U);
  if (first_separator == std::string_view::npos || first_separator == 0U ||
      second_separator == std::string_view::npos || second_separator == first_separator + 1U ||
      token.find('.', second_separator + 1U) != std::string_view::npos) {
    return std::nullopt;
  }
  const auto payload =
    decode_base64url(token.substr(first_separator + 1U, second_separator - first_separator - 1U));
  if (!payload.has_value()) {
    return std::nullopt;
  }
  try {
    const auto document = nlohmann::json::parse(*payload);
    if (!document.is_object() || !document.contains("exp") ||
        !document.at("exp").is_number_integer()) {
      return std::nullopt;
    }
    core::CredentialBundle credential{std::string(token), document.at("exp").get<std::int64_t>()};
    return core::is_credential_usable(credential, now_epoch_seconds)
             ? std::optional<core::CredentialBundle>{std::move(credential)}
             : std::nullopt;
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
}

} // namespace

std::optional<core::CredentialBundle> load_credential_file(const std::filesystem::path &path,
                                                           std::string_view expiry_policy,
                                                           std::int64_t now_epoch_seconds) {
  std::error_code error;
  if (!path.is_absolute() ||
      std::filesystem::symlink_status(path, error).type() != std::filesystem::file_type::regular) {
    return std::nullopt;
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0U || size > kMaximumCredentialFileSize) {
    return std::nullopt;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  while (!contents.empty() && (contents.back() == '\n' || contents.back() == '\r')) {
    contents.pop_back();
  }
  std::optional<core::CredentialBundle> credential;
  if (expiry_policy == "token_expiry") {
    credential = parse_token_expiry(contents, now_epoch_seconds);
  } else if (expiry_policy == "jwt_exp") {
    credential = parse_jwt(contents, now_epoch_seconds);
  }
  secure_wipe(contents);
  return credential;
}

} // namespace office3ds::bridge_host
