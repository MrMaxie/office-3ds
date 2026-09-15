#pragma once

#include "office3ds/core/credential_bundle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace office3ds::platform {

enum class CredentialStatus : std::uint8_t {
  usable,
  missing,
  expired,
  invalid,
  unavailable,
};

class CredentialPolicy {
public:
  static constexpr std::size_t maximum_origin_size = 2048;
  static constexpr std::size_t maximum_bearer_size = 16 * 1024;

  [[nodiscard]] static bool valid_api_origin(std::string_view api_origin);
  [[nodiscard]] static CredentialStatus evaluate(const core::CredentialBundle &credential,
                                                 std::int64_t now_epoch_seconds);
};

class CredentialStore {
public:
  virtual ~CredentialStore() = default;

  [[nodiscard]] virtual std::optional<core::CredentialBundle> load() = 0;
  [[nodiscard]] virtual bool save(const core::CredentialBundle &credential) = 0;
  virtual void clear() noexcept = 0;
};

enum class CredentialClaimStatus : std::uint8_t {
  pending,
  accepted,
  rejected,
  unavailable,
  invalid_response,
};

struct CredentialClaimResult {
  CredentialClaimStatus status = CredentialClaimStatus::pending;
  std::optional<core::CredentialBundle> credential;
};

class CredentialClaimSource {
public:
  virtual ~CredentialClaimSource() = default;

  [[nodiscard]] virtual CredentialClaimResult poll() = 0;
  virtual void cancel() noexcept = 0;
};

} // namespace office3ds::platform
