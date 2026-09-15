#include "office3ds/app/session_views.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace office3ds::app {
namespace {

constexpr float kBackgroundDepth = 0.0F;
constexpr float kControlDepth = 0.25F;
constexpr float kTextDepth = 0.5F;

struct Rectangle {
  float left;
  float top;
  float width;
  float height;
};

Rectangle rectangle(const LoginRectangle &value) {
  return {static_cast<float>(value.left), static_cast<float>(value.top),
          static_cast<float>(value.width), static_cast<float>(value.height)};
}

void fill(const Rectangle &value, float depth, u32 color) {
  C2D_DrawRectSolid(value.left, value.top, depth, value.width, value.height, color);
}

void frame(const Rectangle &value, u32 fill_color, const NativeTheme &theme) {
  fill(value, kControlDepth, theme.border);
  fill({value.left + 2.0F, value.top + 2.0F, value.width - 4.0F, value.height - 4.0F},
       kControlDepth + 0.01F, fill_color);
}

void draw_background(C2D_SpriteSheet sheet) {
  if (sheet != nullptr) {
    C2D_DrawImageAt(C2D_SpriteSheetGetImage(sheet, 0), 0.0F, 0.0F, kBackgroundDepth, nullptr, 1.0F,
                    1.0F);
  }
}

void text(const BitmapFont &font, std::string_view value, float x, float y, BitmapFontScale scale,
          u32 alignment, const NativeTheme &theme) {
  font.draw(value, std::floor(x), std::floor(y), scale, alignment, kTextDepth, theme.text);
}

void centered_text(const BitmapFont &font, std::string_view value, const Rectangle &bounds,
                   BitmapFontScale scale, const NativeTheme &theme) {
  const auto left = std::floor(bounds.left + (bounds.width - font.measure(value, scale)) / 2.0F);
  const auto top = std::floor(bounds.top + (bounds.height - font.line_height(scale)) / 2.0F);
  text(font, value, left, top, scale, C2D_AlignLeft, theme);
}

std::string uppercase_ascii(std::string value) {
  for (auto &character : value) {
    if (character >= 'a' && character <= 'z') {
      character = static_cast<char>(character - 'a' + 'A');
    }
  }
  return value;
}

const char *login_message(core::LoginMessage message) {
  switch (message) {
  case core::LoginMessage::enter_pairing_details:
    return "ENTER PAIRING CODE, IP AND PORT";
  case core::LoginMessage::invalid_pairing_details:
    return "CHECK THE LOCAL PAIRING DETAILS";
  case core::LoginMessage::pairing_rejected:
    return "PAIRING CODE WAS REJECTED";
  case core::LoginMessage::pairing_unavailable:
    return "PAIRING BRIDGE IS UNAVAILABLE";
  case core::LoginMessage::session_expired:
    return "SESSION EXPIRED - PAIR AGAIN";
  case core::LoginMessage::camera_unavailable:
    return "CAMERA UNAVAILABLE - ENTER MANUALLY";
  case core::LoginMessage::scanning_qr:
    return "POINT THE CAMERA AT THE BRIDGE QR";
  case core::LoginMessage::invalid_qr_payload:
    return "THIS IS NOT A VALID PAIRING QR";
  }
  return "ENTER PAIRING CODE, IP AND PORT";
}

std::string field_value(const core::PairingEntry &entry, core::PairingEntryField field) {
  switch (field) {
  case core::PairingEntryField::code:
    return entry.code_text().empty() ? "--------" : std::string(entry.code_text());
  case core::PairingEntryField::ip:
    return entry.ip_text().empty() ? "---.---.---.---" : std::string(entry.ip_text());
  case core::PairingEntryField::port:
    return entry.port_text().empty() ? "-----" : std::string(entry.port_text());
  }
  return {};
}

void draw_prelogin_bottom(const BitmapFont &font, C2D_SpriteSheet background,
                          const NativeTheme &theme, const NativeCopy &copy, bool has_saved_session,
                          core::PreloginMessage message) {
  draw_background(background);
  frame({8.0F, 8.0F, 304.0F, 104.0F}, theme.surface, theme);
  frame(rectangle(kPreloginOpenButton), theme.primary, theme);
  if (has_saved_session) {
    frame(rectangle(kPreloginDeleteButton), theme.surface_high, theme);
  }

  text(font, uppercase_ascii(copy.product_name), 18.0F, 18.0F, BitmapFontScale::x2, C2D_AlignLeft,
       theme);
  text(font, has_saved_session ? "SESSION READY" : "NO SAVED SESSION", 18.0F, 60.0F,
       BitmapFontScale::x1, C2D_AlignLeft, theme);
  text(font, has_saved_session ? "OPEN SAVED DASHBOARD" : "PAIR THIS CONSOLE", 18.0F, 82.0F,
       BitmapFontScale::x1, C2D_AlignLeft, theme);
  centered_text(font, "OPEN OFFICE (A)", rectangle(kPreloginOpenButton), BitmapFontScale::x1,
                theme);
  if (has_saved_session) {
    centered_text(font, "DELETE SESSION (X)", rectangle(kPreloginDeleteButton), BitmapFontScale::x1,
                  theme);
  }
  if (message == core::PreloginMessage::session_delete_failed) {
    text(font, "SESSION DELETE FAILED", 166.0F, 196.0F, BitmapFontScale::x1, C2D_AlignLeft, theme);
  }
  text(font, "START: EXIT", 312.0F, 222.0F, BitmapFontScale::x1, C2D_AlignRight, theme);
}

void draw_login_top(const BitmapFont &font, C2D_SpriteSheet background, const NativeTheme &theme,
                    const NativeCopy &copy, const core::ApplicationFlow &state,
                    const core::PairingEntry &entry) {
  draw_background(background);
  frame({8.0F, 8.0F, 384.0F, 54.0F}, theme.surface, theme);
  frame({8.0F, 72.0F, 384.0F, 74.0F}, theme.surface, theme);
  frame({8.0F, 156.0F, 254.0F, 76.0F}, theme.surface, theme);
  frame({272.0F, 156.0F, 120.0F, 76.0F}, theme.surface, theme);

  constexpr std::array<core::PairingEntryField, 3> fields{
    core::PairingEntryField::code,
    core::PairingEntryField::ip,
    core::PairingEntryField::port,
  };
  constexpr std::array<float, 3> lefts{18.0F, 18.0F, 282.0F};
  constexpr std::array<float, 3> tops{80.0F, 164.0F, 164.0F};
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (entry.field() == fields[index]) {
      fill({lefts[index] - 6.0F, tops[index], 3.0F, 54.0F}, kControlDepth + 0.02F, theme.secondary);
    }
  }

  text(font, uppercase_ascii(copy.product_name) + " PAIRING", 18.0F, 16.0F, BitmapFontScale::x2,
       C2D_AlignLeft, theme);
  text(font, login_message(state.login_message()), 18.0F, 42.0F, BitmapFontScale::x1, C2D_AlignLeft,
       theme);
  constexpr std::array<const char *, 3> labels{"PAIRING CODE", "IP", "PORT"};
  constexpr std::array<float, 3> value_tops{110.0F, 198.0F, 198.0F};
  for (std::size_t index = 0; index < fields.size(); ++index) {
    text(font, labels[index], lefts[index], tops[index], BitmapFontScale::x1, C2D_AlignLeft, theme);
    text(font, field_value(entry, fields[index]), lefts[index], value_tops[index],
         BitmapFontScale::x2, C2D_AlignLeft, theme);
  }
}

void draw_login_bottom(const BitmapFont &font, C2D_SpriteSheet background, const NativeTheme &theme,
                       const core::PairingEntry &entry, const LoginControlFocus &focus) {
  draw_background(background);
  constexpr std::array<const char *, 3> labels{"CODE", "IP", "PORT"};
  for (std::size_t index = 0; index < kLoginFieldTabs.size(); ++index) {
    const auto field = static_cast<core::PairingEntryField>(index);
    frame(rectangle(kLoginFieldTabs[index]), entry.field() == field ? theme.primary : theme.surface,
          theme);
  }
  for (std::size_t index = 0; index < kLoginKeyBounds.size(); ++index) {
    const auto disabled_dot = index == 9 && entry.field() != core::PairingEntryField::ip;
    const auto focused = focus.kind == LoginControlKind::key && focus.index == index;
    frame(rectangle(kLoginKeyBounds[index]),
          focused ? theme.secondary : (disabled_dot ? theme.muted : theme.surface), theme);
  }
  const auto erase = rectangle(kLoginEraseButton);
  const auto qr = rectangle(kLoginQrButton);
  const auto accept = rectangle(kLoginAcceptButton);
  frame(erase, focus.kind == LoginControlKind::erase ? theme.secondary : theme.surface, theme);
  frame(qr, focus.kind == LoginControlKind::qr ? theme.primary : theme.surface_high, theme);
  frame(accept, focus.kind == LoginControlKind::accept ? theme.secondary : theme.primary, theme);

  for (std::size_t index = 0; index < kLoginFieldTabs.size(); ++index) {
    centered_text(font, labels[index], rectangle(kLoginFieldTabs[index]), BitmapFontScale::x1,
                  theme);
  }
  for (std::size_t index = 0; index < kLoginKeyBounds.size(); ++index) {
    centered_text(font, kLoginKeyLabels[index], rectangle(kLoginKeyBounds[index]),
                  BitmapFontScale::x1, theme);
  }
  centered_text(font, "DELETE (B)", erase, BitmapFontScale::x1, theme);
  centered_text(font, "SCAN QR (X)", qr, BitmapFontScale::x1, theme);
  centered_text(font, "PAIR (A)", accept, BitmapFontScale::x1, theme);
  text(font, "TOUCH OR USE THE CONTROLS", 8.0F, 205.0F, BitmapFontScale::x1, C2D_AlignLeft, theme);
}

void draw_qr_scanner_top(const BitmapFont &font, const NativeTheme &theme, C3D_Tex *preview_texture,
                         bool preview_ready) {
  if (preview_texture != nullptr && preview_ready) {
    static const Tex3DS_SubTexture preview_subtexture{
      400, 240, 0.0F, 1.0F, 400.0F / 512.0F, 1.0F - 240.0F / 256.0F,
    };
    C2D_DrawImageAt({preview_texture, &preview_subtexture}, 0.0F, 0.0F, kBackgroundDepth, nullptr,
                    1.0F, 1.0F);
  } else {
    fill({0.0F, 0.0F, 400.0F, 240.0F}, kBackgroundDepth, theme.background);
  }

  constexpr float guide_left = 104.0F;
  constexpr float guide_top = 34.0F;
  constexpr float guide_width = 192.0F;
  constexpr float guide_height = 172.0F;
  constexpr float corner = 28.0F;
  constexpr float thickness = 3.0F;
  for (const auto x : {guide_left, guide_left + guide_width - corner}) {
    fill({x, guide_top, corner, thickness}, kControlDepth, theme.secondary);
    fill({x, guide_top + guide_height - thickness, corner, thickness}, kControlDepth,
         theme.secondary);
  }
  for (const auto x : {guide_left, guide_left + guide_width - thickness}) {
    fill({x, guide_top, thickness, corner}, kControlDepth, theme.secondary);
    fill({x, guide_top + guide_height - corner, thickness, corner}, kControlDepth, theme.secondary);
  }
  fill({0.0F, 208.0F, 400.0F, 32.0F}, kControlDepth, theme.surface);
  if (!preview_ready) {
    text(font, "STARTING CAMERA", 200.0F, 104.0F, BitmapFontScale::x2, C2D_AlignCenter, theme);
  }
  text(font, "SCAN THE QR SHOWN BY THE BRIDGE", 200.0F, 216.0F, BitmapFontScale::x1,
       C2D_AlignCenter, theme);
}

void draw_qr_scanner_bottom(const BitmapFont &font, C2D_SpriteSheet background,
                            const NativeTheme &theme) {
  draw_background(background);
  const Rectangle cancel{56.0F, 154.0F, 208.0F, 42.0F};
  frame(cancel, theme.surface, theme);
  text(font, "QR PAIRING", 160.0F, 62.0F, BitmapFontScale::x2, C2D_AlignCenter, theme);
  text(font, "KEEP THE CODE INSIDE THE FRAME", 160.0F, 108.0F, BitmapFontScale::x1, C2D_AlignCenter,
       theme);
  centered_text(font, "CANCEL (B)", cancel, BitmapFontScale::x1, theme);
}

const char *loading_stage_label(const core::LoadingProgress &progress) {
  if (progress.claiming_credential && progress.completed == 0) {
    return "CLAIMING SESSION";
  }
  switch (progress.stage) {
  case api::DashboardLoadStage::profile:
    return "LOADING PROFILE";
  case api::DashboardLoadStage::worklog:
    return "LOADING WORKLOG";
  case api::DashboardLoadStage::absences:
    return "LOADING ABSENCES";
  case api::DashboardLoadStage::activity:
    return "LOADING ACTIVITY";
  case api::DashboardLoadStage::complete:
    return "PREPARING DASHBOARD";
  }
  return "LOADING DASHBOARD";
}

void draw_loading_top(const BitmapFont &font, C2D_SpriteSheet background, const NativeTheme &theme,
                      const core::ApplicationFlow &state) {
  draw_background(background);
  frame({20.0F, 72.0F, 360.0F, 96.0F}, theme.surface, theme);
  fill({70.0F, 136.0F, 260.0F, 12.0F}, kControlDepth + 0.02F, theme.track);
  const auto &progress = state.loading_progress();
  const auto width =
    state.loading_state() == core::LoadingState::loading && progress.total > 0
      ? 260.0F * static_cast<float>(progress.completed) / static_cast<float>(progress.total)
      : 260.0F;
  fill({70.0F, 136.0F, width, 12.0F}, kControlDepth + 0.03F,
       state.loading_state() == core::LoadingState::loading ? theme.primary : theme.secondary);
  text(font,
       state.loading_state() == core::LoadingState::loading ? loading_stage_label(progress)
                                                            : "SERVICE IS UNAVAILABLE",
       70.0F, 90.0F, BitmapFontScale::x1, C2D_AlignLeft, theme);
  if (state.loading_state() == core::LoadingState::unavailable) {
    text(font,
         state.loading_context() == core::LoadingContext::refresh ? "RETRY (A)   BACK (B)"
                                                                  : "RETRY (A)   PAIRING (B)",
         200.0F, 178.0F, BitmapFontScale::x1, C2D_AlignCenter, theme);
  }
}

} // namespace

bool SessionViews::initialize(const BitmapFont &font, NativeTheme theme, NativeCopy copy) {
  font_ = &font;
  theme_ = theme;
  copy_ = std::move(copy);
  prelogin_background_ = C2D_SpriteSheetLoad("romfs:/prelogin_background.t3x");
  login_background_ = C2D_SpriteSheetLoad("romfs:/login_background.t3x");
  general_top_background_ = C2D_SpriteSheetLoad("romfs:/general_top_background.t3x");
  general_bottom_background_ = C2D_SpriteSheetLoad("romfs:/general_bottom_background.t3x");
  return prelogin_background_ != nullptr && login_background_ != nullptr &&
         general_top_background_ != nullptr && general_bottom_background_ != nullptr &&
         prelogin_scene_.initialize();
}

void SessionViews::shutdown() {
  prelogin_scene_.shutdown();
  const std::array<C2D_SpriteSheet *, 4> sheets{
    &prelogin_background_,
    &login_background_,
    &general_top_background_,
    &general_bottom_background_,
  };
  for (auto *sheet : sheets) {
    if (*sheet != nullptr) {
      C2D_SpriteSheetFree(*sheet);
      *sheet = nullptr;
    }
  }
  font_ = nullptr;
}

void SessionViews::render(const core::ApplicationFlow &state, const core::PairingEntry &entry,
                          const LoginControlFocus &login_focus, bool has_saved_session,
                          C3D_RenderTarget *top_left_target, C3D_RenderTarget *top_right_target,
                          C3D_RenderTarget *bottom_target, float stereo_strength, bool qr_scanning,
                          C3D_Tex *qr_preview_texture, bool qr_preview_ready) {
  if (state.view() == core::ClientView::prelogin) {
    prelogin_scene_.render(top_left_target, stereo_strength, false);
    prelogin_scene_.render(top_right_target, stereo_strength, true);
  }

  C2D_Prepare();
  const auto draw_top = [&](C3D_RenderTarget *target) {
    C2D_TargetClear(target, theme_.background);
    C2D_SceneBegin(target);
    if (state.view() == core::ClientView::login && qr_scanning) {
      draw_qr_scanner_top(*font_, theme_, qr_preview_texture, qr_preview_ready);
    } else if (state.view() == core::ClientView::login) {
      draw_login_top(*font_, login_background_, theme_, copy_, state, entry);
    } else {
      draw_loading_top(*font_, general_top_background_, theme_, state);
    }
  };
  if (state.view() != core::ClientView::prelogin) {
    draw_top(top_left_target);
    draw_top(top_right_target);
  }

  C2D_TargetClear(bottom_target, theme_.background);
  C2D_SceneBegin(bottom_target);
  if (state.view() == core::ClientView::prelogin) {
    draw_prelogin_bottom(*font_, prelogin_background_, theme_, copy_, has_saved_session,
                         state.prelogin_message());
  } else if (state.view() == core::ClientView::login && qr_scanning) {
    draw_qr_scanner_bottom(*font_, general_bottom_background_, theme_);
  } else if (state.view() == core::ClientView::login) {
    draw_login_bottom(*font_, general_bottom_background_, theme_, entry, login_focus);
  } else {
    draw_background(general_bottom_background_);
  }
}

} // namespace office3ds::app
