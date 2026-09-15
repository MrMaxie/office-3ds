#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace office3ds::bridge {

struct PairingEndpoint {
  std::string host;
  std::uint16_t port = 0;
  std::string secret;
};

[[nodiscard]] bool is_private_ipv4(std::string_view value);
[[nodiscard]] bool is_local_bridge_ipv4(std::string_view value);
[[nodiscard]] bool is_valid_pairing_code(std::string_view value);
[[nodiscard]] bool parse_pairing_endpoint(std::string_view value, PairingEndpoint &endpoint);
[[nodiscard]] std::string encode_pairing_payload(const PairingEndpoint &endpoint);
[[nodiscard]] std::optional<PairingEndpoint> decode_pairing_payload(std::string_view payload);

} // namespace office3ds::bridge
