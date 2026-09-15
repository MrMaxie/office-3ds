#pragma once

#include <optional>
#include <string>

#include <citro2d.h>
#include <citro3d.h>

#include "office3ds/api/models.hpp"
#include "office3ds/app/avatar_images.hpp"
#include "office3ds/app/bitmap_font.hpp"
#include "office3ds/app/login_controls.hpp"
#include "office3ds/app/native_theme.hpp"
#include "office3ds/core/dashboard_state.hpp"
#include "office3ds/render_3d/worklog_scene.hpp"

namespace office3ds::app {

inline constexpr LoginRectangle kDashboardHomeButton{8, 190, 96, 42};
inline constexpr LoginRectangle kDashboardRefreshButton{112, 190, 96, 42};
inline constexpr LoginRectangle kDashboardActionButton{216, 190, 96, 42};

class DashboardApp {
public:
  bool initialize(const BitmapFont &font, const AvatarImages &avatars, NativeTheme theme,
                  NativeCopy copy);
  void shutdown();

  void move_subview(int offset);
  void select_subview(core::DashboardSubview subview);
  void move_selection(int offset);
  void move_recognition(int offset);
  std::optional<api::RecognitionRequest> begin_recognition(std::string request_id);
  void mark_recognition_success();
  void mark_recognition_failure();
  [[nodiscard]] core::DashboardSubview subview() const;

  void set_snapshot(api::DashboardSnapshot snapshot);
  void clear_snapshot();
  void mark_unpaired();
  void mark_pairing_failed();
  void mark_offline();
  [[nodiscard]] bool has_snapshot() const;
  [[nodiscard]] const api::DashboardSnapshot *snapshot() const;
  [[nodiscard]] bool handle_touch(int x, int y);
  void render(C3D_RenderTarget *top_left_target, C3D_RenderTarget *top_right_target,
              C3D_RenderTarget *bottom_target, float stereo_strength);

private:
  const BitmapFont *font_{};
  const AvatarImages *avatars_{};
  core::DashboardNavigation navigation_{};
  core::DashboardState state_{};
  NativeTheme theme_{};
  NativeCopy copy_{};
  C2D_SpriteSheet top_background_{};
  C2D_SpriteSheet bottom_background_{};
  render_3d::WorklogScene worklog_scene_{};
};

} // namespace office3ds::app
