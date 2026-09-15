#pragma once

#include <citro2d.h>
#include <citro3d.h>

#include "office3ds/app/bitmap_font.hpp"
#include "office3ds/app/login_controls.hpp"
#include "office3ds/app/native_theme.hpp"
#include "office3ds/core/application_flow.hpp"
#include "office3ds/core/pairing_entry.hpp"
#include "office3ds/render_3d/prelogin_scene.hpp"

namespace office3ds::app {

class SessionViews {
public:
  bool initialize(const BitmapFont &font, NativeTheme theme, NativeCopy copy);
  void shutdown();

  void render(const core::ApplicationFlow &state, const core::PairingEntry &entry,
              const LoginControlFocus &login_focus, bool has_saved_session,
              C3D_RenderTarget *top_left_target, C3D_RenderTarget *top_right_target,
              C3D_RenderTarget *bottom_target, float stereo_strength, bool qr_scanning,
              C3D_Tex *qr_preview_texture, bool qr_preview_ready);

private:
  const BitmapFont *font_{};
  NativeTheme theme_{};
  NativeCopy copy_{};
  C2D_SpriteSheet prelogin_background_{};
  C2D_SpriteSheet login_background_{};
  C2D_SpriteSheet general_top_background_{};
  C2D_SpriteSheet general_bottom_background_{};
  render_3d::PreloginScene prelogin_scene_{};
};

inline constexpr LoginRectangle kPreloginOpenButton{8, 132, 304, 42};
inline constexpr LoginRectangle kPreloginDeleteButton{8, 184, 148, 38};

} // namespace office3ds::app
