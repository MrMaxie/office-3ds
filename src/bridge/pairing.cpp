#include "office3ds/bridge/pairing.hpp"

#include <array>
#include <charconv>
#include <system_error>

namespace office3ds::bridge {
namespace {

constexpr std::string_view kPrefix = "office-3ds://pair?host=";
constexpr std::string_view kPortMarker = "&port=";
constexpr std::string_view kSecretMarker = "&secret=";
constexpr std::string_view kVersionMarker = "&version=";
constexpr std::string_view kProtocolVersion = "2";
constexpr std::size_t kMaximumPayloadLength = 512;
constexpr std::size_t kMaximumSecretLength = 128;

std::optional<std::array<unsigned int, 4>> parse_ipv4(std::string_view value) {
  std::array<unsigned int, 4> octets{};
  std::size_t offset = 0;
  for (std::size_t part_index = 0; part_index < octets.size(); ++part_index) {
    const auto separator = value.find('.', offset);
    const auto part = value.substr(
      offset, separator == std::string_view::npos ? value.size() - offset : separator - offset);
    if (part.empty() || part.size() > 3 || (part.size() > 1 && part.front() == '0')) {
      return std::nullopt;
    }
    const auto result = std::from_chars(part.data(), part.data() + part.size(), octets[part_index]);
    if (result.ec != std::errc{} || result.ptr != part.data() + part.size() ||
        octets[part_index] > 255) {
      return std::nullopt;
    }
    if (part_index + 1 == octets.size()) {
      return separator == std::string_view::npos ? std::optional{octets} : std::nullopt;
    }
    if (separator == std::string_view::npos) {
      return std::nullopt;
    }
    offset = separator + 1;
  }
  return std::nullopt;
}

bool valid_secret(std::string_view value) {
  return value.size() >= 32 && value.size() <= kMaximumSecretLength &&
         value.find_first_not_of("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") == std::string_view::npos;
}

} // namespace

bool is_private_ipv4(std::string_view value) {
  const auto octets = parse_ipv4(value);
  return octets.has_value() &&
         ((*octets)[0] == 10 || ((*octets)[0] == 172 && (*octets)[1] >= 16 && (*octets)[1] <= 31) ||
          ((*octets)[0] == 192 && (*octets)[1] == 168));
}

bool is_local_bridge_ipv4(std::string_view value) {
  const auto octets = parse_ipv4(value);
  return is_private_ipv4(value) || (octets.has_value() && (*octets)[0] == 127);
}

bool is_valid_pairing_code(std::string_view value) {
  return value.size() == 8 && value.find_first_not_of("0123456789") == std::string_view::npos;
}

bool parse_pairing_endpoint(std::string_view value, PairingEndpoint &endpoint) {
  const auto separator = value.rfind(':');
  if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size()) {
    return false;
  }
  const auto host = value.substr(0, separator);
  const auto port_text = value.substr(separator + 1);
  std::uint16_t port = 0;
  const auto parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
  if (!is_local_bridge_ipv4(host) || parsed.ec != std::errc{} ||
      parsed.ptr != port_text.data() + port_text.size() || port == 0) {
    return false;
  }
  endpoint = {std::string(host), port, {}};
  return true;
}

std::string encode_pairing_payload(const PairingEndpoint &endpoint) {
  if (!is_local_bridge_ipv4(endpoint.host) || endpoint.port == 0 ||
      !valid_secret(endpoint.secret)) {
    return {};
  }
  return std::string(kPrefix) + endpoint.host + std::string(kPortMarker) +
         std::to_string(endpoint.port) + std::string(kSecretMarker) + endpoint.secret +
         std::string(kVersionMarker) + std::string(kProtocolVersion);
}

std::optional<PairingEndpoint> decode_pairing_payload(std::string_view payload) {
  if (payload.size() > kMaximumPayloadLength || payload.substr(0, kPrefix.size()) != kPrefix) {
    return std::nullopt;
  }
  const auto port_marker = payload.find(kPortMarker, kPrefix.size());
  const auto secret_marker = payload.find(kSecretMarker, port_marker);
  const auto version_marker = payload.find(kVersionMarker, secret_marker);
  if (port_marker == std::string_view::npos || secret_marker == std::string_view::npos ||
      version_marker == std::string_view::npos || port_marker == kPrefix.size() ||
      payload.find('&', version_marker + kVersionMarker.size()) != std::string_view::npos ||
      payload.substr(version_marker + kVersionMarker.size()) != kProtocolVersion) {
    return std::nullopt;
  }
  const auto host = payload.substr(kPrefix.size(), port_marker - kPrefix.size());
  const auto port = payload.substr(port_marker + kPortMarker.size(),
                                   secret_marker - port_marker - kPortMarker.size());
  const auto secret = payload.substr(secret_marker + kSecretMarker.size(),
                                     version_marker - secret_marker - kSecretMarker.size());
  PairingEndpoint endpoint;
  if (!parse_pairing_endpoint(std::string(host) + ':' + std::string(port), endpoint) ||
      !valid_secret(secret)) {
    return std::nullopt;
  }
  endpoint.secret = std::string(secret);
  return endpoint;
}

} // namespace office3ds::bridge
