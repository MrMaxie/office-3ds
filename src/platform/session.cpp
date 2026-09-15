#include "office3ds/platform/session.hpp"

#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace office3ds::platform {
namespace {

api::DashboardLoadUpdate failedLoad(api::AdapterResult result) {
  return {api::DashboardLoadStage::complete, 0, 4, true, result, std::nullopt};
}

api::RecognitionResult failedRecognition(api::AdapterResult result) {
  return {result, false, {}, {}};
}

} // namespace

ProductSession::ProductSession(api::HttpTransport &transport, CredentialStore &credential_store,
                               std::unique_ptr<api::Adapter> adapter, std::string api_origin)
    : transport_(transport), credential_store_(credential_store),
      api_origin_(std::move(api_origin)), adapter_(std::move(adapter)) {
  if (!adapter_ || !CredentialPolicy::valid_api_origin(api_origin_)) {
    throw std::invalid_argument("Product session requires an adapter and valid API origin");
  }
}

ProductSession::~ProductSession() { shutdown(); }

SessionState ProductSession::state() const noexcept { return state_; }

CredentialStatus ProductSession::activate_credential(const core::CredentialBundle &credential,
                                                     std::int64_t now_epoch_seconds) {
  direct_client_.reset();
  device_expiry_epoch_seconds_ = 0;
  const auto status = CredentialPolicy::evaluate(credential, now_epoch_seconds);
  if (status != CredentialStatus::usable) {
    return status;
  }
  try {
    const auto device_expiry = core::credential_device_expiry_epoch_seconds(credential);
    if (!device_expiry.has_value()) {
      return CredentialStatus::invalid;
    }
    direct_client_ =
      std::make_unique<api::DirectApiClient>(transport_, api_origin_, credential.access_token);
    device_expiry_epoch_seconds_ = *device_expiry;
    state_ = SessionState::ready;
    return CredentialStatus::usable;
  } catch (const std::exception &) {
    direct_client_.reset();
    device_expiry_epoch_seconds_ = 0;
    return CredentialStatus::invalid;
  }
}

CredentialStatus ProductSession::resume(std::int64_t now_epoch_seconds) {
  if (state_ == SessionState::stopped) {
    return CredentialStatus::unavailable;
  }
  cancel_claim();
  dashboard_load_.reset();
  direct_client_.reset();
  device_expiry_epoch_seconds_ = 0;
  try {
    const auto credential = credential_store_.load();
    if (!credential.has_value()) {
      state_ = SessionState::unpaired;
      return CredentialStatus::missing;
    }
    const auto status = activate_credential(*credential, now_epoch_seconds);
    if (status == CredentialStatus::expired || status == CredentialStatus::invalid) {
      credential_store_.clear();
      state_ = status == CredentialStatus::expired ? SessionState::expired : SessionState::unpaired;
    }
    return status;
  } catch (const std::exception &) {
    state_ = SessionState::offline;
    return CredentialStatus::unavailable;
  }
}

bool ProductSession::begin_credential_claim(
  std::unique_ptr<CredentialClaimSource> claim_source) noexcept {
  if (state_ == SessionState::stopped || !claim_source) {
    return false;
  }
  cancel_claim();
  dashboard_load_.reset();
  direct_client_.reset();
  device_expiry_epoch_seconds_ = 0;
  claim_source_ = std::move(claim_source);
  state_ = SessionState::claiming;
  return true;
}

CredentialClaimStatus ProductSession::advance_credential_claim(std::int64_t now_epoch_seconds) {
  if (state_ != SessionState::claiming || !claim_source_) {
    return CredentialClaimStatus::invalid_response;
  }

  CredentialClaimResult result;
  try {
    result = claim_source_->poll();
  } catch (const std::exception &) {
    result.status = CredentialClaimStatus::unavailable;
  }
  if (result.status == CredentialClaimStatus::pending) {
    return result.status;
  }

  if (result.status != CredentialClaimStatus::accepted) {
    claim_source_->cancel();
  }
  claim_source_.reset();
  if (result.status != CredentialClaimStatus::accepted || !result.credential.has_value()) {
    state_ = result.status == CredentialClaimStatus::unavailable ? SessionState::offline
                                                                 : SessionState::unpaired;
    return result.status == CredentialClaimStatus::accepted
             ? CredentialClaimStatus::invalid_response
             : result.status;
  }

  const auto credential_status = activate_credential(*result.credential, now_epoch_seconds);
  if (credential_status != CredentialStatus::usable) {
    state_ = credential_status == CredentialStatus::expired ? SessionState::expired
                                                            : SessionState::unpaired;
    return CredentialClaimStatus::invalid_response;
  }
  try {
    if (!credential_store_.save(*result.credential)) {
      expire_credential();
      state_ = SessionState::offline;
      return CredentialClaimStatus::unavailable;
    }
  } catch (const std::exception &) {
    expire_credential();
    state_ = SessionState::offline;
    return CredentialClaimStatus::unavailable;
  }
  return CredentialClaimStatus::accepted;
}

bool ProductSession::credential_usable(std::int64_t now_epoch_seconds) {
  if (!direct_client_ || device_expiry_epoch_seconds_ <= now_epoch_seconds) {
    expire_credential();
    return false;
  }
  return true;
}

api::AdapterResult ProductSession::begin_dashboard_load(std::string_view today,
                                                        std::int64_t now_epoch_seconds) {
  if (state_ == SessionState::stopped || !adapter_) {
    return api::AdapterResult::invalid_response;
  }
  if (state_ != SessionState::ready && state_ != SessionState::current &&
      state_ != SessionState::offline) {
    return api::AdapterResult::unauthorized;
  }
  if (!credential_usable(now_epoch_seconds)) {
    return api::AdapterResult::unauthorized;
  }
  dashboard_load_ = adapter_->begin_dashboard_load(today);
  if (!dashboard_load_) {
    state_ = SessionState::offline;
    return api::AdapterResult::invalid_response;
  }
  state_ = SessionState::loading;
  return api::AdapterResult::ok;
}

api::DashboardLoadUpdate ProductSession::advance_dashboard_load(std::int64_t now_epoch_seconds) {
  if (state_ != SessionState::loading || !dashboard_load_ || !adapter_) {
    return failedLoad(api::AdapterResult::invalid_response);
  }
  if (!credential_usable(now_epoch_seconds)) {
    dashboard_load_.reset();
    return failedLoad(api::AdapterResult::unauthorized);
  }

  api::DashboardLoadUpdate update;
  try {
    update = dashboard_load_->advance(direct_client_->executor());
  } catch (const std::exception &) {
    dashboard_load_.reset();
    state_ = SessionState::offline;
    return failedLoad(api::AdapterResult::invalid_response);
  }
  if (update.result == api::AdapterResult::unauthorized) {
    dashboard_load_.reset();
    expire_credential();
  } else if (update.result != api::AdapterResult::ok) {
    dashboard_load_.reset();
    state_ = SessionState::offline;
  } else if (update.finished) {
    dashboard_load_.reset();
    state_ =
      update.result == api::AdapterResult::ok ? SessionState::current : SessionState::offline;
  }
  return update;
}

api::RecognitionResult ProductSession::send_recognition(const api::RecognitionRequest &request,
                                                        std::int64_t now_epoch_seconds) {
  if (state_ == SessionState::stopped || !adapter_) {
    return failedRecognition(api::AdapterResult::invalid_response);
  }
  if (state_ != SessionState::current) {
    return failedRecognition(api::AdapterResult::unauthorized);
  }
  if (!credential_usable(now_epoch_seconds)) {
    return failedRecognition(api::AdapterResult::unauthorized);
  }

  api::RecognitionResult result;
  try {
    result = adapter_->send_recognition(request, direct_client_->executor());
  } catch (const std::exception &) {
    state_ = SessionState::offline;
    return failedRecognition(api::AdapterResult::invalid_response);
  }
  if (result.result == api::AdapterResult::unauthorized) {
    expire_credential();
  } else if (result.result == api::AdapterResult::unavailable) {
    state_ = SessionState::offline;
  }
  return result;
}

void ProductSession::cancel_claim() noexcept {
  if (claim_source_) {
    claim_source_->cancel();
    claim_source_.reset();
  }
}

void ProductSession::expire_credential() noexcept {
  cancel_claim();
  dashboard_load_.reset();
  direct_client_.reset();
  device_expiry_epoch_seconds_ = 0;
  credential_store_.clear();
  state_ = SessionState::expired;
}

void ProductSession::clear_credential() noexcept {
  if (state_ != SessionState::stopped) {
    expire_credential();
    state_ = SessionState::unpaired;
  }
}

void ProductSession::shutdown() noexcept {
  if (state_ == SessionState::stopped) {
    return;
  }
  cancel_claim();
  dashboard_load_.reset();
  direct_client_.reset();
  adapter_.reset();
  device_expiry_epoch_seconds_ = 0;
  state_ = SessionState::stopped;
}

} // namespace office3ds::platform
