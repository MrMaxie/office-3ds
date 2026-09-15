#include "office3ds/bridge/pairing_session.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#elif defined(__3DS__)
#include <3ds.h>
#else
#include <sys/random.h>
#endif

namespace office3ds::bridge {
namespace {

constexpr std::string_view kPairingAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static_assert(kPairingAlphabet.size() == 32);
constexpr std::chrono::seconds kMaximumLifetime{15 * 60};
constexpr std::uint8_t kMaximumFailedAttempts = 5;

void wipe(std::string &value) noexcept {
  volatile char *data = value.empty() ? nullptr : value.data();
  for (std::size_t index = 0; data != nullptr && index < value.size(); ++index) {
    data[index] = '\0';
  }
  value.clear();
}

std::string make_pairing_code(std::string value) {
  for (char &character : value) {
    character = static_cast<char>('0' + (static_cast<unsigned char>(character) % 10));
  }
  return value;
}

bool equals_secret(std::string_view left, std::string_view right) {
  const std::size_t size = std::max(left.size(), right.size());
  unsigned char difference = static_cast<unsigned char>(left.size() ^ right.size());
  for (std::size_t index = 0; index < size; ++index) {
    const auto left_value = index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
    const auto right_value = index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
    difference |= static_cast<unsigned char>(left_value ^ right_value);
  }
  return difference == 0;
}

bool fill_random(std::uint8_t *bytes, std::size_t size) {
#if defined(_WIN32)
  if (size > std::numeric_limits<ULONG>::max()) {
    return false;
  }
  return BCryptGenRandom(nullptr, bytes, static_cast<ULONG>(size),
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__3DS__)
  if (R_FAILED(psInit())) {
    return false;
  }
  const auto result = PS_GenerateRandomBytes(bytes, size);
  psExit();
  return R_SUCCEEDED(result);
#else
  std::size_t offset = 0;
  while (offset < size) {
    const auto received = getrandom(bytes + offset, size - offset, 0);
    if (received < 0 && errno == EINTR) {
      continue;
    }
    if (received <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(received);
  }
  return true;
#endif
}

} // namespace

std::string SystemRandomSource::token(std::size_t length) {
  std::vector<std::uint8_t> random(length);
  if (!fill_random(random.data(), random.size())) {
    return {};
  }
  std::string result(length, '\0');
  for (std::size_t index = 0; index < random.size(); ++index) {
    result[index] = kPairingAlphabet[random[index] & 31U];
  }
  return result;
}

PairingSession::PairingSession(RandomSource &random_source) : random_source_(random_source) {}

std::optional<PairingOffer> PairingSession::create_offer(std::string host, std::uint16_t port,
                                                         std::chrono::system_clock::time_point now,
                                                         std::chrono::seconds lifetime) {
  if (!is_local_bridge_ipv4(host) || port == 0 || lifetime <= std::chrono::seconds::zero() ||
      lifetime > kMaximumLifetime) {
    return std::nullopt;
  }
  Pending pending{random_source_.token(32), make_pairing_code(random_source_.token(8)),
                  now + lifetime, 0};
  if (pending.secret.size() != 32 || pending.pairing_code.size() != 8) {
    return std::nullopt;
  }
  PairingOffer offer{encode_pairing_payload({std::move(host), port, pending.secret}),
                     pending.pairing_code, pending.expires_at};
  if (offer.qr_payload.empty()) {
    return std::nullopt;
  }
  invalidate();
  pending_ = std::move(pending);
  return offer;
}

bool PairingSession::is_available(std::chrono::system_clock::time_point now) {
  if (pending_.has_value() && now < pending_->expires_at) {
    return true;
  }
  invalidate();
  return false;
}

bool PairingSession::consume_secret(std::string_view secret,
                                    std::chrono::system_clock::time_point now) {
  return consume(secret, false, now);
}

bool PairingSession::consume_pairing_code(std::string_view code,
                                          std::chrono::system_clock::time_point now) {
  return consume(code, true, now);
}

void PairingSession::invalidate() {
  if (pending_.has_value()) {
    wipe(pending_->secret);
    wipe(pending_->pairing_code);
  }
  pending_.reset();
}

bool PairingSession::consume(std::string_view value, bool uses_pairing_code,
                             std::chrono::system_clock::time_point now) {
  if (!is_available(now)) {
    return false;
  }
  const auto &expected = uses_pairing_code ? pending_->pairing_code : pending_->secret;
  if (!equals_secret(value, expected)) {
    ++pending_->failed_attempts;
    if (pending_->failed_attempts >= kMaximumFailedAttempts) {
      invalidate();
    }
    return false;
  }
  invalidate();
  return true;
}

} // namespace office3ds::bridge
