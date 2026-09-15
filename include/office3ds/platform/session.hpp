#pragma once

#include "office3ds/api/direct_api_client.hpp"
#include "office3ds/platform/credential.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace office3ds::platform {

enum class SessionState : std::uint8_t {
  unpaired,
  claiming,
  ready,
  loading,
  current,
  offline,
  expired,
  stopped,
};

class ProductSession {
public:
  ProductSession(api::HttpTransport &transport, CredentialStore &credential_store,
                 std::unique_ptr<api::Adapter> adapter, std::string api_origin);
  ~ProductSession();

  ProductSession(const ProductSession &) = delete;
  ProductSession &operator=(const ProductSession &) = delete;

  [[nodiscard]] SessionState state() const noexcept;
  [[nodiscard]] CredentialStatus resume(std::int64_t now_epoch_seconds);

  [[nodiscard]] bool
  begin_credential_claim(std::unique_ptr<CredentialClaimSource> claim_source) noexcept;
  [[nodiscard]] CredentialClaimStatus advance_credential_claim(std::int64_t now_epoch_seconds);

  [[nodiscard]] api::AdapterResult begin_dashboard_load(std::string_view today,
                                                        std::int64_t now_epoch_seconds);
  [[nodiscard]] api::DashboardLoadUpdate advance_dashboard_load(std::int64_t now_epoch_seconds);
  [[nodiscard]] api::RecognitionResult send_recognition(const api::RecognitionRequest &request,
                                                        std::int64_t now_epoch_seconds);

  void clear_credential() noexcept;
  void shutdown() noexcept;

private:
  [[nodiscard]] CredentialStatus activate_credential(const core::CredentialBundle &credential,
                                                     std::int64_t now_epoch_seconds);
  [[nodiscard]] bool credential_usable(std::int64_t now_epoch_seconds);
  void cancel_claim() noexcept;
  void expire_credential() noexcept;

  api::HttpTransport &transport_;
  CredentialStore &credential_store_;
  std::string api_origin_;
  std::unique_ptr<api::Adapter> adapter_;
  std::unique_ptr<CredentialClaimSource> claim_source_;
  std::unique_ptr<api::DirectApiClient> direct_client_;
  std::unique_ptr<api::DashboardLoad> dashboard_load_;
  std::int64_t device_expiry_epoch_seconds_ = 0;
  SessionState state_ = SessionState::unpaired;
};

} // namespace office3ds::platform
