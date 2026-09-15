#pragma once

#include "office3ds/bridge/pairing.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace office3ds::bridge {

class RandomSource {
public:
  virtual ~RandomSource() = default;
  [[nodiscard]] virtual std::string token(std::size_t length) = 0;
};

class SystemRandomSource final : public RandomSource {
public:
  [[nodiscard]] std::string token(std::size_t length) override;
};

struct PairingOffer {
  std::string qr_payload;
  std::string pairing_code;
  std::chrono::system_clock::time_point expires_at;
};

class PairingSession {
public:
  explicit PairingSession(RandomSource &random_source);

  [[nodiscard]] std::optional<PairingOffer> create_offer(std::string host, std::uint16_t port,
                                                         std::chrono::system_clock::time_point now,
                                                         std::chrono::seconds lifetime);
  [[nodiscard]] bool is_available(std::chrono::system_clock::time_point now);
  [[nodiscard]] bool consume_secret(std::string_view secret,
                                    std::chrono::system_clock::time_point now);
  [[nodiscard]] bool consume_pairing_code(std::string_view code,
                                          std::chrono::system_clock::time_point now);
  void invalidate();

private:
  struct Pending {
    std::string secret;
    std::string pairing_code;
    std::chrono::system_clock::time_point expires_at;
    std::uint8_t failed_attempts = 0;
  };

  [[nodiscard]] bool consume(std::string_view value, bool uses_pairing_code,
                             std::chrono::system_clock::time_point now);

  RandomSource &random_source_;
  std::optional<Pending> pending_;
};

} // namespace office3ds::bridge
