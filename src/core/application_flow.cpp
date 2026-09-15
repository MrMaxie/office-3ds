#include "office3ds/core/application_flow.hpp"

namespace office3ds::core {

ClientView ApplicationFlow::view() const noexcept { return view_; }

PreloginMessage ApplicationFlow::prelogin_message() const noexcept { return prelogin_message_; }

LoginMessage ApplicationFlow::login_message() const noexcept { return login_message_; }

LoadingState ApplicationFlow::loading_state() const noexcept { return loading_state_; }

LoadingContext ApplicationFlow::loading_context() const noexcept { return loading_context_; }

const LoadingProgress &ApplicationFlow::loading_progress() const noexcept {
  return loading_progress_;
}

void ApplicationFlow::activate(bool has_valid_credential) noexcept {
  if (has_valid_credential) {
    start_loading();
  } else {
    show_login();
  }
}

void ApplicationFlow::show_prelogin(PreloginMessage message) noexcept {
  view_ = ClientView::prelogin;
  prelogin_message_ = message;
  loading_state_ = LoadingState::loading;
}

void ApplicationFlow::show_login(LoginMessage message) noexcept {
  view_ = ClientView::login;
  login_message_ = message;
  loading_state_ = LoadingState::loading;
}

void ApplicationFlow::start_loading(LoadingContext context) noexcept {
  view_ = ClientView::loading;
  loading_state_ = LoadingState::loading;
  loading_context_ = context;
  loading_progress_ = {};
  loading_progress_.claiming_credential = context == LoadingContext::pairing;
  loading_progress_.total = context == LoadingContext::pairing ? 5 : 4;
}

bool ApplicationFlow::set_loading_progress(LoadingProgress progress) noexcept {
  if (view_ != ClientView::loading || loading_state_ != LoadingState::loading ||
      progress.total != loading_progress_.total || progress.completed > progress.total ||
      progress.completed < loading_progress_.completed ||
      (loading_progress_.claiming_credential && progress.completed == 0 &&
       !progress.claiming_credential)) {
    return false;
  }
  loading_progress_ = progress;
  return true;
}

void ApplicationFlow::show_loading_unavailable() noexcept {
  view_ = ClientView::loading;
  loading_state_ = LoadingState::unavailable;
}

void ApplicationFlow::show_dashboard() noexcept {
  view_ = ClientView::dashboard;
  loading_state_ = LoadingState::loading;
}

} // namespace office3ds::core
