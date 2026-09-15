#pragma once

#include "office3ds/api/models.hpp"

#include <cstddef>

namespace office3ds::core {

enum class ClientView {
  prelogin,
  login,
  loading,
  dashboard,
};

enum class LoginMessage {
  enter_pairing_details,
  invalid_pairing_details,
  pairing_rejected,
  pairing_unavailable,
  session_expired,
  camera_unavailable,
  scanning_qr,
  invalid_qr_payload,
};

enum class PreloginMessage {
  none,
  session_delete_failed,
};

enum class LoadingState {
  loading,
  unavailable,
};

enum class LoadingContext {
  resume,
  pairing,
  refresh,
};

struct LoadingProgress {
  std::size_t completed = 0;
  std::size_t total = 4;
  api::DashboardLoadStage stage = api::DashboardLoadStage::profile;
  bool claiming_credential = false;
};

class ApplicationFlow {
public:
  [[nodiscard]] ClientView view() const noexcept;
  [[nodiscard]] PreloginMessage prelogin_message() const noexcept;
  [[nodiscard]] LoginMessage login_message() const noexcept;
  [[nodiscard]] LoadingState loading_state() const noexcept;
  [[nodiscard]] LoadingContext loading_context() const noexcept;
  [[nodiscard]] const LoadingProgress &loading_progress() const noexcept;

  void activate(bool has_valid_credential) noexcept;
  void show_prelogin(PreloginMessage message = PreloginMessage::none) noexcept;
  void show_login(LoginMessage message = LoginMessage::enter_pairing_details) noexcept;
  void start_loading(LoadingContext context = LoadingContext::resume) noexcept;
  [[nodiscard]] bool set_loading_progress(LoadingProgress progress) noexcept;
  void show_loading_unavailable() noexcept;
  void show_dashboard() noexcept;

private:
  ClientView view_ = ClientView::prelogin;
  PreloginMessage prelogin_message_ = PreloginMessage::none;
  LoginMessage login_message_ = LoginMessage::enter_pairing_details;
  LoadingState loading_state_ = LoadingState::loading;
  LoadingContext loading_context_ = LoadingContext::resume;
  LoadingProgress loading_progress_{};
};

} // namespace office3ds::core
