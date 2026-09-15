#include "office3ds/app/login_controls.hpp"

#include <algorithm>
#include <limits>

namespace office3ds::app {
namespace {

constexpr std::array<LoginControlFocus, 17> kLoginControls{
  LoginControlFocus{LoginControlKind::field, 0},  LoginControlFocus{LoginControlKind::field, 1},
  LoginControlFocus{LoginControlKind::field, 2},  LoginControlFocus{LoginControlKind::key, 0},
  LoginControlFocus{LoginControlKind::key, 1},    LoginControlFocus{LoginControlKind::key, 2},
  LoginControlFocus{LoginControlKind::key, 3},    LoginControlFocus{LoginControlKind::key, 4},
  LoginControlFocus{LoginControlKind::key, 5},    LoginControlFocus{LoginControlKind::key, 6},
  LoginControlFocus{LoginControlKind::key, 7},    LoginControlFocus{LoginControlKind::key, 8},
  LoginControlFocus{LoginControlKind::key, 9},    LoginControlFocus{LoginControlKind::key, 10},
  LoginControlFocus{LoginControlKind::erase, 0},  LoginControlFocus{LoginControlKind::qr, 0},
  LoginControlFocus{LoginControlKind::accept, 0},
};

bool valid_focus(const LoginControlFocus &focus) {
  if (focus.kind == LoginControlKind::field) {
    return focus.index < kLoginFieldTabs.size();
  }
  if (focus.kind == LoginControlKind::key) {
    return focus.index < kLoginKeyBounds.size();
  }
  return true;
}

} // namespace

bool contains(const LoginRectangle &rectangle, int x, int y) {
  return x >= rectangle.left && x < rectangle.left + rectangle.width && y >= rectangle.top &&
         y < rectangle.top + rectangle.height;
}

const LoginRectangle &login_control_bounds(const LoginControlFocus &focus) {
  if (focus.kind == LoginControlKind::field && focus.index < kLoginFieldTabs.size()) {
    return kLoginFieldTabs[focus.index];
  }
  if (focus.kind == LoginControlKind::key && focus.index < kLoginKeyBounds.size()) {
    return kLoginKeyBounds[focus.index];
  }
  if (focus.kind == LoginControlKind::erase) {
    return kLoginEraseButton;
  }
  if (focus.kind == LoginControlKind::qr) {
    return kLoginQrButton;
  }
  if (focus.kind == LoginControlKind::accept) {
    return kLoginAcceptButton;
  }
  return kLoginFieldTabs.front();
}

std::optional<LoginControlFocus> login_control_at(int x, int y) {
  const auto control =
    std::find_if(kLoginControls.begin(), kLoginControls.end(), [x, y](const auto &candidate) {
      return contains(login_control_bounds(candidate), x, y);
    });
  return control == kLoginControls.end() ? std::nullopt
                                         : std::optional<LoginControlFocus>(*control);
}

std::optional<core::PairingEntryField> login_field_for_control(const LoginControlFocus &focus) {
  if (focus.kind != LoginControlKind::field || focus.index >= kLoginFieldTabs.size()) {
    return std::nullopt;
  }
  return static_cast<core::PairingEntryField>(focus.index);
}

std::optional<char> login_key_for_control(const LoginControlFocus &focus) {
  if (focus.kind != LoginControlKind::key || focus.index >= kLoginKeyValues.size()) {
    return std::nullopt;
  }
  return kLoginKeyValues[focus.index];
}

void LoginNavigation::reset() { focus_ = {}; }

const LoginControlFocus &LoginNavigation::focus() const { return focus_; }

bool LoginNavigation::set_focus(LoginControlFocus focus) {
  if (!valid_focus(focus) || focus == focus_) {
    return false;
  }
  focus_ = focus;
  return true;
}

bool LoginNavigation::move(LoginDirection direction) {
  const auto &origin = login_control_bounds(focus_);
  const auto origin_x = origin.left * 2 + origin.width;
  const auto origin_y = origin.top * 2 + origin.height;
  auto best = focus_;
  auto best_score = std::numeric_limits<int>::max();
  auto best_secondary = std::numeric_limits<int>::max();

  for (const auto &candidate : kLoginControls) {
    if (candidate == focus_) {
      continue;
    }
    const auto &bounds = login_control_bounds(candidate);
    const auto delta_x = bounds.left * 2 + bounds.width - origin_x;
    const auto delta_y = bounds.top * 2 + bounds.height - origin_y;
    const auto horizontal = direction == LoginDirection::left || direction == LoginDirection::right;
    const auto primary_delta = horizontal ? delta_x : delta_y;
    const auto secondary = horizontal ? std::abs(delta_y) : std::abs(delta_x);
    const auto sign = direction == LoginDirection::left || direction == LoginDirection::up ? -1 : 1;
    const auto primary = primary_delta * sign;
    if (primary <= 0) {
      continue;
    }
    const auto score = primary + secondary * 3;
    if (score < best_score || (score == best_score && secondary < best_secondary)) {
      best = candidate;
      best_score = score;
      best_secondary = secondary;
    }
  }
  return set_focus(best);
}

} // namespace office3ds::app
