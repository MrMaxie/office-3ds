#pragma once

#include "office3ds/core/pairing_entry.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace office3ds::app {

struct LoginRectangle {
  int left;
  int top;
  int width;
  int height;
};

enum class LoginControlKind {
  field,
  key,
  erase,
  qr,
  accept,
};

struct LoginControlFocus {
  LoginControlKind kind = LoginControlKind::field;
  std::size_t index = 0;
};

constexpr bool operator==(const LoginControlFocus &left, const LoginControlFocus &right) {
  return left.kind == right.kind && left.index == right.index;
}

constexpr bool operator!=(const LoginControlFocus &left, const LoginControlFocus &right) {
  return !(left == right);
}

enum class LoginDirection {
  left,
  right,
  up,
  down,
};

inline constexpr std::array<LoginRectangle, 3> kLoginFieldTabs{
  LoginRectangle{8, 8, 96, 28},
  LoginRectangle{112, 8, 96, 28},
  LoginRectangle{216, 8, 96, 28},
};

inline constexpr std::array<LoginRectangle, 11> kLoginKeyBounds{
  LoginRectangle{8, 48, 58, 28},    LoginRectangle{76, 48, 58, 28},
  LoginRectangle{144, 48, 58, 28},  LoginRectangle{8, 82, 58, 28},
  LoginRectangle{76, 82, 58, 28},   LoginRectangle{144, 82, 58, 28},
  LoginRectangle{8, 116, 58, 28},   LoginRectangle{76, 116, 58, 28},
  LoginRectangle{144, 116, 58, 28}, LoginRectangle{8, 150, 58, 28},
  LoginRectangle{76, 150, 126, 28},
};

inline constexpr std::array<char, 11> kLoginKeyValues{'1', '2', '3', '4', '5', '6',
                                                      '7', '8', '9', '.', '0'};
inline constexpr std::array<const char *, 11> kLoginKeyLabels{
  "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0",
};

inline constexpr LoginRectangle kLoginEraseButton{212, 48, 100, 28};
inline constexpr LoginRectangle kLoginQrButton{212, 82, 100, 28};
inline constexpr LoginRectangle kLoginAcceptButton{212, 116, 100, 62};

[[nodiscard]] bool contains(const LoginRectangle &rectangle, int x, int y);
[[nodiscard]] const LoginRectangle &login_control_bounds(const LoginControlFocus &focus);
[[nodiscard]] std::optional<LoginControlFocus> login_control_at(int x, int y);
[[nodiscard]] std::optional<core::PairingEntryField>
login_field_for_control(const LoginControlFocus &focus);
[[nodiscard]] std::optional<char> login_key_for_control(const LoginControlFocus &focus);

class LoginNavigation {
public:
  void reset();
  [[nodiscard]] const LoginControlFocus &focus() const;
  bool set_focus(LoginControlFocus focus);
  bool move(LoginDirection direction);

private:
  LoginControlFocus focus_{};
};

} // namespace office3ds::app
