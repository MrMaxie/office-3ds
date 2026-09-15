#include "office3ds/app/dashboard_app.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

constexpr std::array<Rectangle, 3> kSubviewTabs{
  Rectangle{8.0F, 130.0F, 96.0F, 48.0F},
  Rectangle{112.0F, 130.0F, 96.0F, 48.0F},
  Rectangle{216.0F, 130.0F, 96.0F, 48.0F},
};

Rectangle rectangle(const LoginRectangle &value) {
  return {static_cast<float>(value.left), static_cast<float>(value.top),
          static_cast<float>(value.width), static_cast<float>(value.height)};
}

void fill(const Rectangle &value, float depth, u32 color) {
  C2D_DrawRectSolid(value.left, value.top, depth, value.width, value.height, color);
}

void frame(const Rectangle &value, u32 fill_color, const NativeTheme &theme, u32 border_color = 0) {
  const auto border = border_color == 0 ? theme.border : border_color;
  fill(value, kControlDepth, border);
  fill({value.left + 2.0F, value.top + 2.0F, value.width - 4.0F, value.height - 4.0F},
       kControlDepth + 0.01F, fill_color);
}

void outline(const Rectangle &value, const NativeTheme &theme) {
  fill({value.left, value.top, value.width, 2.0F}, kControlDepth, theme.border);
  fill({value.left, value.top + value.height - 2.0F, value.width, 2.0F}, kControlDepth,
       theme.border);
  fill({value.left, value.top, 2.0F, value.height}, kControlDepth, theme.border);
  fill({value.left + value.width - 2.0F, value.top, 2.0F, value.height}, kControlDepth,
       theme.border);
}

void draw_background(C2D_SpriteSheet sheet) {
  if (sheet != nullptr) {
    C2D_DrawImageAt(C2D_SpriteSheetGetImage(sheet, 0), 0.0F, 0.0F, kBackgroundDepth, nullptr, 1.0F,
                    1.0F);
  }
}

void text(const BitmapFont &font, std::string_view value, float x, float y,
          const NativeTheme &theme, BitmapFontScale scale = BitmapFontScale::x1,
          u32 alignment = C2D_AlignLeft, u32 color = 0) {
  font.draw(value, std::floor(x), std::floor(y), scale, alignment, kTextDepth,
            color == 0 ? theme.text : color);
}

void fitted_text(const BitmapFont &font, std::string_view value, float x, float y,
                 float maximum_width, const NativeTheme &theme,
                 BitmapFontScale maximum_scale = BitmapFontScale::x1, u32 color = 0) {
  const auto scale =
    fit_bitmap_font_scale(font.measure(value, BitmapFontScale::x1), maximum_width, maximum_scale);
  text(font, value, x, y, theme, scale, C2D_AlignLeft, color);
}

void centered_text(const BitmapFont &font, std::string_view value, const Rectangle &bounds,
                   const NativeTheme &theme, BitmapFontScale scale = BitmapFontScale::x1) {
  text(font, value, bounds.left + bounds.width / 2.0F,
       bounds.top + (bounds.height - font.line_height(scale)) / 2.0F, theme, scale,
       C2D_AlignCenter);
}

std::string uppercase_ascii(std::string value) {
  for (auto &character : value) {
    if (character >= 'a' && character <= 'z') {
      character = static_cast<char>(character - 'a' + 'A');
    }
  }
  return value;
}

std::string initials_for(std::string_view name) {
  std::string initials;
  bool at_word_start = true;
  for (const auto character : name) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      at_word_start = true;
    } else if (at_word_start && initials.size() < 2) {
      initials += static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
      at_word_start = false;
    } else {
      at_word_start = false;
    }
  }
  return initials.empty() ? "OF" : initials;
}

std::string format_minutes(std::int32_t minutes) {
  const auto safe = std::max(minutes, std::int32_t{0});
  return std::to_string(safe / 60) + "h " + std::to_string(safe % 60) + "m";
}

std::string date_label(std::string_view iso_date) {
  return iso_date.size() >= 10 ? std::string(iso_date.substr(5, 5)) : std::string(iso_date);
}

const char *connection_label(core::ConnectionState state) {
  switch (state) {
  case core::ConnectionState::unpaired:
    return "PAIR";
  case core::ConnectionState::pairing_failed:
    return "TRY AGAIN";
  case core::ConnectionState::current:
    return nullptr;
  case core::ConnectionState::offline:
    return "OFFLINE";
  }
  return nullptr;
}

std::string absence_group_label(std::string_view group) {
  if (group == "today") {
    return "TODAY";
  }
  if (group == "next_two_weeks" || group == "upcoming") {
    return "NEXT 2 WEEKS";
  }
  if (group == "within_six_weeks" || group == "planned") {
    return "WITHIN 6 WEEKS";
  }
  if (group == "none_planned") {
    return "NO PLANNED LEAVE";
  }
  return uppercase_ascii(std::string(group.empty() ? "PLANNED" : group));
}

u32 absence_group_color(std::string_view group, const NativeTheme &theme) {
  if (group == "today") {
    return theme.primary;
  }
  if (group == "next_two_weeks" || group == "upcoming") {
    return theme.secondary;
  }
  if (group == "within_six_weeks" || group == "planned") {
    return theme.success;
  }
  return theme.muted;
}

void draw_initials_frame(float left, float top, float size, u32 color, const NativeTheme &theme,
                         bool selected = false) {
  frame({left, top, size, size}, color, theme, selected ? theme.primary : theme.text);
}

void draw_avatar(const BitmapFont &font, const AvatarImages &avatars, std::string_view avatar_url,
                 float left, float top, float size, std::string_view display_name,
                 const NativeTheme &theme) {
  if (const auto *image = avatars.find(avatar_url); image != nullptr) {
    C2D_DrawImageAt(*image, left, top, kTextDepth, nullptr, size / image->subtex->width,
                    size / image->subtex->height);
    return;
  }
  text(font, initials_for(display_name), left + size / 2.0F,
       top + (size - font.line_height(BitmapFontScale::x1)) / 2.0F, theme, BitmapFontScale::x1,
       C2D_AlignCenter);
}

void draw_header_geometry(const NativeTheme &theme) {
  frame({8.0F, 8.0F, 384.0F, 28.0F}, theme.surface, theme);
  fill({14.0F, 14.0F, 4.0F, 16.0F}, kControlDepth + 0.02F, theme.primary);
}

void draw_header_text(const BitmapFont &font, std::string_view label,
                      core::ConnectionState connection, const NativeTheme &theme) {
  text(font, label, 26.0F, 14.0F, theme);
  if (const auto *status = connection_label(connection); status != nullptr) {
    text(font, status, 382.0F, 14.0F, theme, BitmapFontScale::x1, C2D_AlignRight);
  }
}

void draw_worklog_plot_base(const NativeTheme &theme) {
  frame({8.0F, 42.0F, 246.0F, 190.0F}, theme.surface, theme);
  constexpr float baseline = 190.0F;
  fill({18.0F, baseline, 222.0F, 2.0F}, kControlDepth + 0.02F, theme.track);
  for (int step = 1; step <= 4; ++step) {
    const auto y = baseline - static_cast<float>(step) * 27.0F;
    fill({18.0F, y, 222.0F, 1.0F}, kControlDepth + 0.02F, theme.surface_high);
  }
}

void draw_worklog(const BitmapFont &font, const api::DashboardSnapshot *snapshot,
                  const core::DashboardNavigation &navigation, core::ConnectionState connection,
                  const NativeTheme &theme, const NativeCopy &copy) {
  draw_header_geometry(theme);
  outline({8.0F, 42.0F, 246.0F, 190.0F}, theme);
  frame({262.0F, 42.0F, 130.0F, 190.0F}, theme.surface, theme);
  draw_header_text(font, uppercase_ascii(copy.worklog) + " - LAST 5 DAYS", connection, theme);
  if (snapshot == nullptr || snapshot->worklog.empty()) {
    text(font, "NO RECENT WORKLOG", 131.0F, 112.0F, theme, BitmapFontScale::x1, C2D_AlignCenter);
    text(font, "--", 327.0F, 112.0F, theme, BitmapFontScale::x1, C2D_AlignCenter);
    return;
  }

  const auto &days = snapshot->worklog;
  const auto visible_count = std::min<std::size_t>(days.size(), 5);
  const auto first_visible = days.size() - visible_count;
  const auto selected_index = std::min(navigation.selected_index(), days.size() - 1);
  for (std::size_t visible_index = 0; visible_index < visible_count; ++visible_index) {
    const auto day_index = first_visible + visible_index;
    const auto left = 26.0F + static_cast<float>(visible_index) * 43.0F;
    text(font, date_label(days[day_index].date), left + 12.0F, 202.0F, theme, BitmapFontScale::x1,
         C2D_AlignCenter, day_index == selected_index ? theme.secondary : theme.text);
  }

  const auto &selected = days[selected_index];
  text(font, date_label(selected.date), 274.0F, 54.0F, theme, BitmapFontScale::x2);
  text(font, "WORKED", 274.0F, 100.0F, theme);
  text(font, format_minutes(selected.minutes), 274.0F, 124.0F, theme, BitmapFontScale::x2);
  text(font, "TARGET", 274.0F, 160.0F, theme);
  text(font, selected.expected_minutes == 0 ? "--" : format_minutes(selected.expected_minutes),
       274.0F, 184.0F, theme, BitmapFontScale::x2);
  if (selected.expected_minutes > 0) {
    const auto percent =
      std::max(selected.minutes, std::int32_t{0}) * 100 / selected.expected_minutes;
    text(font, std::to_string(percent) + "%", 274.0F, 214.0F, theme);
  }
}

void draw_absences(const BitmapFont &font, const api::DashboardSnapshot *snapshot,
                   const core::DashboardNavigation &navigation, core::ConnectionState connection,
                   const AvatarImages &avatars, const NativeTheme &theme, const NativeCopy &copy) {
  draw_header_geometry(theme);
  if (snapshot == nullptr || snapshot->absences.empty()) {
    frame({8.0F, 42.0F, 384.0F, 190.0F}, theme.surface, theme);
    draw_header_text(font, uppercase_ascii(copy.absences), connection, theme);
    text(font, "NO PLANNED ABSENCES", 200.0F, 112.0F, theme, BitmapFontScale::x1, C2D_AlignCenter);
    return;
  }

  const auto &people = snapshot->absences;
  const auto selected_index = std::min(navigation.selected_index(), people.size() - 1);
  const auto first_visible = selected_index > 2 ? selected_index - 2 : 0;
  const auto visible_count = std::min<std::size_t>(3, people.size() - first_visible);
  for (std::size_t visible_index = 0; visible_index < visible_count; ++visible_index) {
    const auto person_index = first_visible + visible_index;
    const auto &person = people[person_index];
    const auto selected = person_index == selected_index;
    const auto top = 42.0F + static_cast<float>(visible_index) * 47.0F;
    const auto color = absence_group_color(person.group, theme);
    frame({8.0F, top, 384.0F, 42.0F}, selected ? theme.surface_high : theme.surface, theme,
          selected ? theme.primary : theme.border);
    fill({11.0F, top + 3.0F, 5.0F, 36.0F}, kControlDepth + 0.02F, color);
    draw_initials_frame(20.0F, top + 7.0F, 28.0F, color, theme, selected);
  }
  frame({8.0F, 194.0F, 384.0F, 38.0F}, theme.surface, theme,
        absence_group_color(people[selected_index].group, theme));

  draw_header_text(font, uppercase_ascii(copy.absences), connection, theme);
  for (std::size_t visible_index = 0; visible_index < visible_count; ++visible_index) {
    const auto person_index = first_visible + visible_index;
    const auto &person = people[person_index];
    const auto top = 42.0F + static_cast<float>(visible_index) * 47.0F;
    const auto detail = person.label.empty() ? "BACK " + date_label(person.return_date)
                                             : uppercase_ascii(person.label);
    draw_avatar(font, avatars, person.avatar_url, 20.0F, top + 7.0F, 28.0F, person.person_name,
                theme);
    fitted_text(font, person.person_name, 58.0F, top + 4.0F, 204.0F, theme);
    fitted_text(font, detail, 58.0F, top + 22.0F, 204.0F, theme);
    text(font, absence_group_label(person.group), 382.0F, top + 13.0F, theme, BitmapFontScale::x1,
         C2D_AlignRight);
  }
  const auto &selected = people[selected_index];
  fitted_text(font, selected.person_name, 18.0F, 197.0F, 364.0F, theme);
  fitted_text(font,
              selected.label.empty() ? "BACK " + date_label(selected.return_date)
                                     : uppercase_ascii(selected.label),
              18.0F, 215.0F, 364.0F, theme);
}

std::string recognition_status_label(core::RecognitionStatus status, const NativeCopy &copy) {
  switch (status) {
  case core::RecognitionStatus::idle:
    return "CHOOSE A VALUE, THEN PRESS A";
  case core::RecognitionStatus::pending:
    return "SENDING " + uppercase_ascii(copy.recognition);
  case core::RecognitionStatus::success:
    return uppercase_ascii(copy.recognition) + " SENT";
  case core::RecognitionStatus::failure:
    return uppercase_ascii(copy.recognition) + " FAILED - TRY AGAIN";
  }
  return "CHOOSE A VALUE";
}

void draw_activity(const BitmapFont &font, const api::DashboardSnapshot *snapshot,
                   const core::DashboardNavigation &navigation, core::ConnectionState connection,
                   const AvatarImages &avatars, const NativeTheme &theme, const NativeCopy &copy) {
  draw_header_geometry(theme);
  if (snapshot == nullptr || snapshot->activity.empty()) {
    frame({8.0F, 42.0F, 384.0F, 144.0F}, theme.surface, theme);
    frame({8.0F, 194.0F, 384.0F, 38.0F}, theme.surface, theme);
    draw_header_text(font, uppercase_ascii(copy.activity), connection, theme);
    text(font, "NO RECENT EVENTS", 200.0F, 94.0F, theme, BitmapFontScale::x1, C2D_AlignCenter);
    text(font, "NOTHING TO SEND", 18.0F, 205.0F, theme);
    return;
  }

  const auto &events = snapshot->activity;
  const auto selected_index = std::min(navigation.selected_index(), events.size() - 1);
  const auto first_visible = selected_index > 2 ? selected_index - 2 : 0;
  const auto visible_count = std::min<std::size_t>(3, events.size() - first_visible);
  for (std::size_t visible_index = 0; visible_index < visible_count; ++visible_index) {
    const auto event_index = first_visible + visible_index;
    const auto &event = events[event_index];
    const auto selected = event_index == selected_index;
    const auto top = 42.0F + static_cast<float>(visible_index) * 49.0F;
    frame({8.0F, top, 384.0F, 44.0F}, selected ? theme.surface_high : theme.surface, theme,
          selected ? theme.secondary : theme.border);
    draw_initials_frame(18.0F, top + 8.0F, 28.0F, selected ? theme.secondary : theme.muted, theme,
                        selected);
    const auto value_count = std::min<std::size_t>(event.allowed_recognition_values.size(), 3);
    for (std::size_t value_index = 0; value_index < value_count; ++value_index) {
      const Rectangle button{306.0F + static_cast<float>(value_index) * 27.0F, top + 7.0F, 24.0F,
                             30.0F};
      frame(button,
            selected && value_index == navigation.selected_recognition_index() ? theme.primary
                                                                               : theme.track,
            theme);
    }
  }
  frame({8.0F, 194.0F, 384.0F, 38.0F}, theme.surface, theme);

  draw_header_text(font, uppercase_ascii(copy.activity), connection, theme);
  for (std::size_t visible_index = 0; visible_index < visible_count; ++visible_index) {
    const auto event_index = first_visible + visible_index;
    const auto &event = events[event_index];
    const auto top = 42.0F + static_cast<float>(visible_index) * 49.0F;
    draw_avatar(font, avatars, event.avatar_url, 18.0F, top + 8.0F, 28.0F, event.recipient_name,
                theme);
    fitted_text(font, event.recipient_name, 54.0F, top + 4.0F, 242.0F, theme);
    fitted_text(font, event.summary, 54.0F, top + 22.0F, 184.0F, theme);
    text(font, event.age_label, 298.0F, top + 22.0F, theme, BitmapFontScale::x1, C2D_AlignRight);
    const auto value_count = std::min<std::size_t>(event.allowed_recognition_values.size(), 3);
    for (std::size_t value_index = 0; value_index < value_count; ++value_index) {
      const Rectangle button{306.0F + static_cast<float>(value_index) * 27.0F, top + 7.0F, 24.0F,
                             30.0F};
      centered_text(font, "+" + std::to_string(event.allowed_recognition_values[value_index]),
                    button, theme);
    }
  }
  text(font, recognition_status_label(navigation.recognition_status(), copy), 18.0F, 205.0F, theme);
}

std::string subview_label(core::DashboardSubview subview, const NativeCopy &copy) {
  switch (subview) {
  case core::DashboardSubview::worklog:
    return uppercase_ascii(copy.worklog);
  case core::DashboardSubview::absences:
    return uppercase_ascii(copy.absences);
  case core::DashboardSubview::activity:
    return uppercase_ascii(copy.activity);
  }
  return uppercase_ascii(copy.worklog);
}

void draw_bottom(const BitmapFont &font, C2D_SpriteSheet background,
                 const api::DashboardSnapshot *snapshot,
                 const core::DashboardNavigation &navigation, core::ConnectionState connection,
                 const AvatarImages &avatars, const NativeTheme &theme, const NativeCopy &copy) {
  draw_background(background);
  frame({8.0F, 8.0F, 304.0F, 96.0F}, theme.surface, theme);
  draw_initials_frame(18.0F, 20.0F, 54.0F, theme.secondary, theme);
  for (std::size_t index = 0; index < kSubviewTabs.size(); ++index) {
    const auto subview = static_cast<core::DashboardSubview>(index);
    frame(kSubviewTabs[index], navigation.subview() == subview ? theme.primary : theme.surface,
          theme);
  }
  frame(rectangle(kDashboardHomeButton), theme.surface, theme);
  frame(rectangle(kDashboardRefreshButton), theme.surface, theme);
  frame(rectangle(kDashboardActionButton),
        navigation.subview() == core::DashboardSubview::activity ? theme.secondary : theme.muted,
        theme);

  const auto display_name =
    snapshot == nullptr ? std::string("NOT SIGNED IN") : snapshot->profile.display_name;
  draw_avatar(font, avatars,
              snapshot == nullptr ? std::string_view{} : snapshot->profile.avatar_url, 18.0F, 20.0F,
              54.0F, display_name, theme);
  fitted_text(font, display_name, 86.0F, 20.0F, 216.0F, theme, BitmapFontScale::x2);
  if (snapshot != nullptr && !snapshot->profile.role.empty()) {
    fitted_text(font, snapshot->profile.role, 86.0F, 52.0F, 216.0F, theme);
  }
  if (const auto *status = connection_label(connection); status != nullptr) {
    text(font, status, 86.0F, 78.0F, theme);
  }
  for (std::size_t index = 0; index < kSubviewTabs.size(); ++index) {
    const auto subview = static_cast<core::DashboardSubview>(index);
    centered_text(font, subview_label(subview, copy), kSubviewTabs[index], theme);
  }
  centered_text(font, "HOME (B)", rectangle(kDashboardHomeButton), theme);
  centered_text(font, "REFRESH (X)", rectangle(kDashboardRefreshButton), theme);
  centered_text(font,
                navigation.subview() == core::DashboardSubview::activity ? "SEND (A)" : "SELECT",
                rectangle(kDashboardActionButton), theme);
}

bool contains(const Rectangle &value, int x, int y) {
  return static_cast<float>(x) >= value.left && static_cast<float>(x) < value.left + value.width &&
         static_cast<float>(y) >= value.top && static_cast<float>(y) < value.top + value.height;
}

} // namespace

bool DashboardApp::initialize(const BitmapFont &font, const AvatarImages &avatars,
                              NativeTheme theme, NativeCopy copy) {
  font_ = &font;
  avatars_ = &avatars;
  theme_ = theme;
  copy_ = std::move(copy);
  top_background_ = C2D_SpriteSheetLoad("romfs:/general_top_background.t3x");
  bottom_background_ = C2D_SpriteSheetLoad("romfs:/general_bottom_background.t3x");
  return top_background_ != nullptr && bottom_background_ != nullptr && worklog_scene_.initialize();
}

void DashboardApp::shutdown() {
  worklog_scene_.shutdown();
  if (top_background_ != nullptr) {
    C2D_SpriteSheetFree(top_background_);
    top_background_ = nullptr;
  }
  if (bottom_background_ != nullptr) {
    C2D_SpriteSheetFree(bottom_background_);
    bottom_background_ = nullptr;
  }
  font_ = nullptr;
  avatars_ = nullptr;
}

void DashboardApp::move_subview(int offset) { navigation_.move_subview(offset); }

void DashboardApp::select_subview(core::DashboardSubview subview) {
  navigation_.select_subview(subview);
}

void DashboardApp::move_selection(int offset) {
  if (const auto *value = state_.snapshot(); value != nullptr) {
    navigation_.move_selection(offset, *value);
  }
}

void DashboardApp::move_recognition(int offset) {
  if (const auto *value = state_.snapshot(); value != nullptr) {
    navigation_.move_recognition(offset, *value);
  }
}

std::optional<api::RecognitionRequest> DashboardApp::begin_recognition(std::string request_id) {
  const auto *value = state_.snapshot();
  return value == nullptr ? std::nullopt
                          : navigation_.begin_recognition(*value, std::move(request_id));
}

void DashboardApp::mark_recognition_success() { navigation_.mark_recognition_success(); }

void DashboardApp::mark_recognition_failure() { navigation_.mark_recognition_failure(); }

core::DashboardSubview DashboardApp::subview() const { return navigation_.subview(); }

void DashboardApp::set_snapshot(api::DashboardSnapshot snapshot) {
  if (state_.has_snapshot()) {
    navigation_.reconcile_snapshot(snapshot);
  } else {
    navigation_.reset_for_snapshot(snapshot);
  }
  state_.set_snapshot(std::move(snapshot));
}

void DashboardApp::clear_snapshot() { state_.clear_snapshot(); }

void DashboardApp::mark_unpaired() {
  state_.clear_snapshot();
  state_.mark_unpaired();
}

void DashboardApp::mark_pairing_failed() { state_.mark_pairing_failed(); }

void DashboardApp::mark_offline() { state_.mark_offline(); }

bool DashboardApp::has_snapshot() const { return state_.has_snapshot(); }

const api::DashboardSnapshot *DashboardApp::snapshot() const { return state_.snapshot(); }

bool DashboardApp::handle_touch(int x, int y) {
  for (std::size_t index = 0; index < kSubviewTabs.size(); ++index) {
    if (contains(kSubviewTabs[index], x, y)) {
      navigation_.select_subview(static_cast<core::DashboardSubview>(index));
      return true;
    }
  }
  return false;
}

void DashboardApp::render(C3D_RenderTarget *top_left_target, C3D_RenderTarget *top_right_target,
                          C3D_RenderTarget *bottom_target, float stereo_strength) {
  C2D_Prepare();
  const auto render_top = [this, stereo_strength](C3D_RenderTarget *target, bool right_eye) {
    C2D_TargetClear(target, theme_.background);
    C2D_SceneBegin(target);
    draw_background(top_background_);
    const auto *value = state_.snapshot();
    if (navigation_.subview() == core::DashboardSubview::worklog) {
      draw_worklog_plot_base(theme_);
      C2D_Flush();
      if (value != nullptr) {
        worklog_scene_.render(target, value->worklog, navigation_.selected_index(), stereo_strength,
                              right_eye);
      }
      C2D_Prepare();
      C2D_SceneBegin(target);
      draw_worklog(*font_, value, navigation_, state_.connection_state(), theme_, copy_);
    } else if (navigation_.subview() == core::DashboardSubview::absences) {
      draw_absences(*font_, value, navigation_, state_.connection_state(), *avatars_, theme_,
                    copy_);
    } else {
      draw_activity(*font_, value, navigation_, state_.connection_state(), *avatars_, theme_,
                    copy_);
    }
  };
  render_top(top_left_target, false);
  render_top(top_right_target, true);

  C2D_TargetClear(bottom_target, theme_.background);
  C2D_SceneBegin(bottom_target);
  draw_bottom(*font_, bottom_background_, state_.snapshot(), navigation_, state_.connection_state(),
              *avatars_, theme_, copy_);
}

} // namespace office3ds::app
