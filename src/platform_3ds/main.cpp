#include "office3ds/app/bitmap_font.hpp"
#include "office3ds/app/dashboard_app.hpp"
#include "office3ds/app/login_controls.hpp"
#include "office3ds/app/native_theme.hpp"
#include "office3ds/app/session_views.hpp"
#include "office3ds/bridge/pairing.hpp"
#include "office3ds/core/application_flow.hpp"
#include "office3ds/core/pairing_entry.hpp"
#include "office3ds/platform/session.hpp"
#include "office3ds/platform_3ds/audio_feedback.hpp"
#include "office3ds/platform_3ds/avatar_store.hpp"
#include "office3ds/platform_3ds/bridge_claim_source.hpp"
#include "office3ds/platform_3ds/credential_store.hpp"
#include "office3ds/platform_3ds/http.hpp"
#include "office3ds/platform_3ds/qr_scanner.hpp"

#include "product_config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

namespace office3ds::generated {
std::unique_ptr<api::Adapter> create_product_adapter();
}

namespace {

constexpr std::size_t kMax2dObjects = 960U;
constexpr std::uint32_t kDisplayTransferFlags =
  GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
  GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
  GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

std::int64_t now_seconds() { return static_cast<std::int64_t>(std::time(nullptr)); }

std::string today() {
  const auto now = std::time(nullptr);
  std::tm value{};
  gmtime_r(&now, &value);
  std::ostringstream output;
  output << std::put_time(&value, "%Y-%m-%d");
  return output.str();
}

std::string request_id() {
  return "office-" + std::to_string(now_seconds()) + '-' + std::to_string(svcGetSystemTick());
}

std::string presentation_value(const std::unordered_map<std::string, std::string> &values,
                               std::string_view name, std::string_view fallback) {
  const auto found = values.find(std::string(name));
  return found == values.end() ? std::string(fallback) : found->second;
}

office3ds::app::NativeTheme native_theme() {
  const auto &values = office3ds::generated::product_presentation().palette;
  const auto color = [&values](std::string_view name, std::string_view fallback) {
    const auto encoded = presentation_value(values, name, fallback);
    return office3ds::app::parse_rgba(encoded, 0xFFFFFFFFU);
  };
  return {
    color("background", "#1C2329"), color("control", "#2A343C"), color("surface_high", "#3A4852"),
    color("track", "#11171B"),      color("muted", "#7E8B94"),   color("accent", "#3B82A6"),
    color("focus", "#E2A93B"),      color("success", "#54B887"), color("text", "#F7FAFC"),
    color("border", "#0D1114"),
  };
}

office3ds::app::NativeCopy native_copy() {
  const auto &descriptor = office3ds::generated::product_descriptor();
  const auto &values = office3ds::generated::product_presentation().copy;
  return {
    descriptor.display_name,
    presentation_value(values, "worklog", "Worklog"),
    presentation_value(values, "absences", "Absences"),
    presentation_value(values, "activity", "Activity"),
    presentation_value(values, "recognition", "Recognition"),
  };
}

bool touched(const office3ds::app::LoginRectangle &bounds, const touchPosition &position) {
  return office3ds::app::contains(bounds, position.px, position.py);
}

void open_login(
  office3ds::core::ApplicationFlow &flow, office3ds::core::PairingEntry &entry,
  office3ds::app::LoginNavigation &navigation,
  office3ds::core::LoginMessage message = office3ds::core::LoginMessage::enter_pairing_details) {
  if (!entry.is_open()) {
    entry.open();
  }
  navigation.reset();
  flow.show_login(message);
}

} // namespace

int main() {
  const auto &descriptor = office3ds::generated::product_descriptor();
  const auto theme = native_theme();
  const auto copy = native_copy();

  gfxInitDefault();
  gfxSet3D(true);
  if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
    gfxExit();
    return 1;
  }
  if (!C2D_Init(kMax2dObjects)) {
    C3D_Fini();
    gfxExit();
    return 1;
  }

  const auto romfs_ready = R_SUCCEEDED(romfsInit());
  auto *top_left = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
  auto *top_right = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
  auto *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  if (top_left != nullptr) {
    C3D_RenderTargetSetOutput(top_left, GFX_TOP, GFX_LEFT, kDisplayTransferFlags);
  }
  if (top_right != nullptr) {
    C3D_RenderTargetSetOutput(top_right, GFX_TOP, GFX_RIGHT, kDisplayTransferFlags);
  }

  office3ds::app::BitmapFont font;
  office3ds::app::SessionViews session_views;
  office3ds::app::DashboardApp dashboard;
  office3ds::platform_3ds::HttpRuntime http;
  office3ds::platform_3ds::AvatarStore avatars(descriptor.slug);
  office3ds::platform_3ds::SdCredentialStore credential_store(descriptor.slug);
  office3ds::platform_3ds::AudioFeedback audio;
  office3ds::platform_3ds::QrScanner scanner;

  const auto initialized = romfs_ready && top_left != nullptr && top_right != nullptr &&
                           bottom != nullptr && http.initialize() &&
                           credential_store.initialize() && avatars.initialize(http) &&
                           font.initialize() && session_views.initialize(font, theme, copy) &&
                           dashboard.initialize(font, avatars, theme, copy);
  if (!initialized) {
    dashboard.shutdown();
    session_views.shutdown();
    font.shutdown();
    avatars.shutdown();
    credential_store.shutdown();
    http.shutdown();
    if (romfs_ready) {
      romfsExit();
    }
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 1;
  }

  (void)audio.initialize();
  office3ds::platform_3ds::CurlHttpTransport transport;
  office3ds::platform::ProductSession session(transport, credential_store,
                                              office3ds::generated::create_product_adapter(),
                                              descriptor.api_base_url);
  office3ds::core::ApplicationFlow flow;
  office3ds::core::PairingEntry pairing_entry;
  office3ds::app::LoginNavigation login_navigation;
  auto credential_status = session.resume(now_seconds());
  auto has_saved_session = credential_status == office3ds::platform::CredentialStatus::usable;
  flow.show_prelogin();
  auto loading_rendered = false;

  const auto start_dashboard_load = [&](office3ds::core::LoadingContext context) {
    flow.start_loading(context);
    loading_rendered = false;
    if (session.begin_dashboard_load(today(), now_seconds()) != office3ds::api::AdapterResult::ok) {
      flow.show_loading_unavailable();
    }
  };

  while (aptMainLoop()) {
    if (flow.view() == office3ds::core::ClientView::loading &&
        flow.loading_state() == office3ds::core::LoadingState::loading && loading_rendered) {
      if (session.state() == office3ds::platform::SessionState::claiming) {
        const auto result = session.advance_credential_claim(now_seconds());
        if (result == office3ds::platform::CredentialClaimStatus::accepted) {
          has_saved_session = true;
          start_dashboard_load(office3ds::core::LoadingContext::pairing);
        } else if (result != office3ds::platform::CredentialClaimStatus::pending) {
          open_login(flow, pairing_entry, login_navigation,
                     result == office3ds::platform::CredentialClaimStatus::rejected
                       ? office3ds::core::LoginMessage::pairing_rejected
                       : office3ds::core::LoginMessage::pairing_unavailable);
          audio.error();
        }
      } else if (session.state() == office3ds::platform::SessionState::loading) {
        const auto update = session.advance_dashboard_load(now_seconds());
        const auto offset = flow.loading_context() == office3ds::core::LoadingContext::pairing
                              ? std::size_t{1}
                              : std::size_t{0};
        (void)flow.set_loading_progress({static_cast<std::size_t>(update.completed) + offset,
                                         static_cast<std::size_t>(update.total) + offset,
                                         update.stage, offset != 0});
        if (update.finished && update.result == office3ds::api::AdapterResult::ok &&
            update.snapshot.has_value()) {
          avatars.load(*update.snapshot);
          dashboard.set_snapshot(*update.snapshot);
          flow.show_dashboard();
          audio.refresh();
        } else if (update.result == office3ds::api::AdapterResult::unauthorized) {
          has_saved_session = false;
          avatars.clear();
          dashboard.mark_unpaired();
          open_login(flow, pairing_entry, login_navigation,
                     office3ds::core::LoginMessage::session_expired);
          audio.error();
        } else if (update.result != office3ds::api::AdapterResult::ok) {
          dashboard.mark_offline();
          flow.show_loading_unavailable();
          audio.error();
        }
      }
      loading_rendered = false;
    }

    hidScanInput();
    const auto pressed = hidKeysDown();
    if ((pressed & KEY_START) != 0) {
      break;
    }
    touchPosition touch{};
    const auto is_touch = (pressed & KEY_TOUCH) != 0;
    if (is_touch) {
      hidTouchRead(&touch);
    }

    if (flow.view() == office3ds::core::ClientView::prelogin) {
      const auto delete_session =
        (pressed & KEY_X) != 0 ||
        (is_touch && has_saved_session && touched(office3ds::app::kPreloginDeleteButton, touch));
      const auto open_office =
        (pressed & KEY_A) != 0 || (is_touch && touched(office3ds::app::kPreloginOpenButton, touch));
      if (delete_session && has_saved_session) {
        session.clear_credential();
        avatars.clear();
        dashboard.mark_unpaired();
        has_saved_session = false;
        flow.show_prelogin();
        audio.select();
      } else if (open_office) {
        if (has_saved_session) {
          start_dashboard_load(office3ds::core::LoadingContext::resume);
          audio.refresh();
        } else {
          pairing_entry.open();
          login_navigation.reset();
          flow.show_login();
          audio.select();
        }
      }
    } else if (flow.view() == office3ds::core::ClientView::login) {
      const auto begin_pairing = [&]() {
        const auto input = pairing_entry.submit();
        if (!input.has_value()) {
          flow.show_login(office3ds::core::LoginMessage::invalid_pairing_details);
          audio.error();
          return;
        }
        flow.start_loading(office3ds::core::LoadingContext::pairing);
        loading_rendered = false;
        if (!session.begin_credential_claim(
              std::make_unique<office3ds::platform_3ds::BridgeClaimSource>(
                input->endpoint, input->pairing_material, input->uses_qr_secret))) {
          open_login(flow, pairing_entry, login_navigation,
                     office3ds::core::LoginMessage::pairing_unavailable);
          audio.error();
        } else {
          audio.refresh();
        }
      };
      const auto activate = [&](const office3ds::app::LoginControlFocus &focus) {
        if (const auto field = office3ds::app::login_field_for_control(focus); field.has_value()) {
          pairing_entry.select_field(*field);
          audio.select();
        } else if (const auto key = office3ds::app::login_key_for_control(focus); key.has_value()) {
          pairing_entry.append(*key) ? audio.select() : audio.error();
        } else if (focus.kind == office3ds::app::LoginControlKind::erase) {
          pairing_entry.erase();
          audio.select();
        } else if (focus.kind == office3ds::app::LoginControlKind::qr) {
          if (scanner.start()) {
            flow.show_login(office3ds::core::LoginMessage::scanning_qr);
            audio.select();
          } else {
            flow.show_login(office3ds::core::LoginMessage::camera_unavailable);
            audio.error();
          }
        } else if (focus.kind == office3ds::app::LoginControlKind::accept) {
          begin_pairing();
        }
      };

      if (scanner.active()) {
        const auto scan = scanner.update();
        if (scan.status == office3ds::platform_3ds::QrScanStatus::decoded) {
          const auto endpoint = office3ds::bridge::decode_pairing_payload(scan.payload);
          if (endpoint.has_value() && pairing_entry.apply_qr_endpoint(*endpoint)) {
            login_navigation.reset();
            begin_pairing();
          } else {
            flow.show_login(office3ds::core::LoginMessage::invalid_qr_payload);
            audio.error();
          }
        } else if (scan.status == office3ds::platform_3ds::QrScanStatus::failed) {
          flow.show_login(office3ds::core::LoginMessage::camera_unavailable);
          audio.error();
        } else if ((pressed & KEY_B) != 0) {
          scanner.stop();
          flow.show_login();
          audio.select();
        }
      } else if ((pressed & KEY_X) != 0) {
        activate({office3ds::app::LoginControlKind::qr, 0});
      } else if ((pressed & KEY_B) != 0) {
        if (pairing_entry.selected_value_empty()) {
          pairing_entry.cancel();
          flow.show_prelogin();
          login_navigation.reset();
        } else {
          pairing_entry.erase();
        }
        audio.select();
      } else if ((pressed & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN | KEY_CPAD_LEFT |
                             KEY_CPAD_RIGHT | KEY_CPAD_UP | KEY_CPAD_DOWN)) != 0) {
        auto direction = office3ds::app::LoginDirection::left;
        if ((pressed & (KEY_RIGHT | KEY_CPAD_RIGHT)) != 0) {
          direction = office3ds::app::LoginDirection::right;
        } else if ((pressed & (KEY_UP | KEY_CPAD_UP)) != 0) {
          direction = office3ds::app::LoginDirection::up;
        } else if ((pressed & (KEY_DOWN | KEY_CPAD_DOWN)) != 0) {
          direction = office3ds::app::LoginDirection::down;
        }
        if (login_navigation.move(direction)) {
          if (const auto field = office3ds::app::login_field_for_control(login_navigation.focus());
              field.has_value()) {
            pairing_entry.select_field(*field);
          }
          audio.select();
        }
      } else if ((pressed & KEY_A) != 0) {
        activate(login_navigation.focus());
      } else if (is_touch) {
        if (const auto control = office3ds::app::login_control_at(touch.px, touch.py);
            control.has_value()) {
          login_navigation.set_focus(*control);
          activate(*control);
        }
      }
    } else if (flow.view() == office3ds::core::ClientView::loading) {
      if (flow.loading_state() == office3ds::core::LoadingState::unavailable) {
        if ((pressed & KEY_A) != 0) {
          start_dashboard_load(flow.loading_context() == office3ds::core::LoadingContext::pairing
                                 ? office3ds::core::LoadingContext::resume
                                 : flow.loading_context());
          audio.refresh();
        } else if ((pressed & KEY_B) != 0) {
          if (flow.loading_context() == office3ds::core::LoadingContext::refresh &&
              dashboard.has_snapshot()) {
            flow.show_dashboard();
          } else {
            open_login(flow, pairing_entry, login_navigation);
          }
          audio.select();
        }
      }
    } else if (flow.view() == office3ds::core::ClientView::dashboard) {
      const auto go_home = (pressed & KEY_B) != 0 ||
                           (is_touch && touched(office3ds::app::kDashboardHomeButton, touch));
      const auto refresh = (pressed & KEY_X) != 0 ||
                           (is_touch && touched(office3ds::app::kDashboardRefreshButton, touch));
      const auto send = (pressed & KEY_A) != 0 ||
                        (is_touch && touched(office3ds::app::kDashboardActionButton, touch));
      if (go_home) {
        flow.show_prelogin();
        audio.select();
      } else if ((pressed & KEY_L) != 0) {
        dashboard.move_subview(-1);
        audio.select();
      } else if ((pressed & KEY_R) != 0) {
        dashboard.move_subview(1);
        audio.select();
      } else {
        const auto horizontal = (pressed & (KEY_LEFT | KEY_CPAD_LEFT)) != 0
                                  ? -1
                                  : ((pressed & (KEY_RIGHT | KEY_CPAD_RIGHT)) != 0 ? 1 : 0);
        const auto vertical = (pressed & (KEY_UP | KEY_CPAD_UP)) != 0
                                ? -1
                                : ((pressed & (KEY_DOWN | KEY_CPAD_DOWN)) != 0 ? 1 : 0);
        if (dashboard.subview() == office3ds::core::DashboardSubview::worklog && horizontal != 0) {
          dashboard.move_selection(horizontal);
          audio.select();
        } else if (dashboard.subview() == office3ds::core::DashboardSubview::absences &&
                   vertical != 0) {
          dashboard.move_selection(vertical);
          audio.select();
        } else if (dashboard.subview() == office3ds::core::DashboardSubview::activity) {
          if (vertical != 0) {
            dashboard.move_selection(vertical);
            audio.select();
          } else if (horizontal != 0) {
            dashboard.move_recognition(horizontal);
            audio.select();
          }
        }

        if (refresh) {
          start_dashboard_load(office3ds::core::LoadingContext::refresh);
          audio.refresh();
        } else if (send && dashboard.subview() == office3ds::core::DashboardSubview::activity) {
          const auto recognition = dashboard.begin_recognition(request_id());
          if (!recognition.has_value()) {
            audio.error();
          } else {
            const auto result = session.send_recognition(*recognition, now_seconds());
            if (result.result == office3ds::api::AdapterResult::ok && result.accepted) {
              dashboard.mark_recognition_success();
              audio.select();
            } else if (result.result == office3ds::api::AdapterResult::unauthorized) {
              has_saved_session = false;
              dashboard.mark_recognition_failure();
              dashboard.mark_unpaired();
              avatars.clear();
              open_login(flow, pairing_entry, login_navigation,
                         office3ds::core::LoginMessage::session_expired);
              audio.error();
            } else {
              dashboard.mark_recognition_failure();
              audio.error();
            }
          }
        } else if (is_touch && dashboard.handle_touch(touch.px, touch.py)) {
          audio.select();
        }
      }
    }

    if (flow.view() == office3ds::core::ClientView::dashboard) {
      avatars.update();
    }
    audio.update();
    const auto stereo_strength = osGet3DSliderState();
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    if (flow.view() == office3ds::core::ClientView::dashboard) {
      dashboard.render(top_left, top_right, bottom, stereo_strength);
    } else {
      session_views.render(flow, pairing_entry, login_navigation.focus(), has_saved_session,
                           top_left, top_right, bottom, stereo_strength, scanner.active(),
                           scanner.preview_texture(), scanner.preview_ready());
    }
    C3D_FrameEnd(0);

    if (flow.view() == office3ds::core::ClientView::loading &&
        flow.loading_state() == office3ds::core::LoadingState::loading) {
      loading_rendered = true;
    } else {
      loading_rendered = false;
    }
  }

  scanner.stop();
  session.shutdown();
  audio.shutdown();
  dashboard.shutdown();
  session_views.shutdown();
  font.shutdown();
  avatars.shutdown();
  credential_store.shutdown();
  http.shutdown();
  romfsExit();
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  return 0;
}
