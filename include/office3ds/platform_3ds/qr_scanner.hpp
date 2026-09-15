#pragma once

#include <cstdint>
#include <string>

#include <3ds.h>
#include <citro3d.h>

struct quirc;

namespace office3ds::platform_3ds {

enum class QrScanStatus {
  scanning,
  decoded,
  failed,
};

struct QrScanUpdate {
  QrScanStatus status = QrScanStatus::scanning;
  std::string payload;
};

class QrScanner {
public:
  ~QrScanner();

  QrScanner(const QrScanner &) = delete;
  QrScanner &operator=(const QrScanner &) = delete;
  QrScanner() = default;

  [[nodiscard]] bool start();
  [[nodiscard]] QrScanUpdate update();
  void stop() noexcept;
  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] bool preview_ready() const noexcept;
  [[nodiscard]] C3D_Tex *preview_texture() noexcept;

private:
  [[nodiscard]] bool configure_camera();
  [[nodiscard]] bool queue_frame();
  [[nodiscard]] bool recover_capture();
  void update_preview();
  [[nodiscard]] std::string decode_frame();

  ::quirc *decoder_ = nullptr;
  std::uint16_t *camera_buffer_ = nullptr;
  C3D_Tex preview_texture_{};
  Handle receive_event_ = 0;
  Handle buffer_error_event_ = 0;
  std::uint32_t transfer_unit_ = 0;
  bool camera_initialized_ = false;
  bool texture_initialized_ = false;
  bool capturing_ = false;
  bool active_ = false;
  bool preview_ready_ = false;
};

} // namespace office3ds::platform_3ds
