#include "office3ds/platform_3ds/qr_scanner.hpp"

#include <algorithm>
#include <cstddef>

#include <quirc.h>

namespace office3ds::platform_3ds {
namespace {

constexpr int kCameraWidth = 400;
constexpr int kCameraHeight = 240;
constexpr int kTextureWidth = 512;
constexpr int kTextureHeight = 256;
constexpr std::size_t kCameraPixelCount = kCameraWidth * kCameraHeight;
constexpr std::size_t kCameraBufferSize = kCameraPixelCount * sizeof(std::uint16_t);
constexpr std::size_t kTextureBufferSize =
  static_cast<std::size_t>(kTextureWidth * kTextureHeight) * sizeof(std::uint16_t);

std::uint8_t grayscale(std::uint16_t pixel) {
  const auto red = static_cast<std::uint16_t>(((pixel >> 11U) & 0x1FU) << 3U);
  const auto green = static_cast<std::uint16_t>(((pixel >> 5U) & 0x3FU) << 2U);
  const auto blue = static_cast<std::uint16_t>((pixel & 0x1FU) << 3U);
  return static_cast<std::uint8_t>((red + green + blue) / 3U);
}

std::size_t tiled_texture_offset(int x, int y) {
  return static_cast<std::size_t>(
    (((y >> 3) * (kTextureWidth >> 3) + (x >> 3)) << 6) +
    ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
}

} // namespace

QrScanner::~QrScanner() { stop(); }

bool QrScanner::start() {
  stop();
  decoder_ = quirc_new();
  if (decoder_ == nullptr || quirc_resize(decoder_, kCameraWidth, kCameraHeight) < 0) {
    stop();
    return false;
  }
  camera_buffer_ = static_cast<std::uint16_t *>(linearAlloc(kCameraBufferSize));
  if (camera_buffer_ == nullptr ||
      !C3D_TexInit(&preview_texture_, kTextureWidth, kTextureHeight, GPU_RGB565)) {
    stop();
    return false;
  }
  texture_initialized_ = true;
  C3D_TexSetFilter(&preview_texture_, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetWrap(&preview_texture_, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
  if (R_FAILED(camInit())) {
    stop();
    return false;
  }
  camera_initialized_ = true;
  if (!configure_camera() || !queue_frame() || R_FAILED(CAMU_StartCapture(PORT_CAM1))) {
    stop();
    return false;
  }
  capturing_ = true;
  active_ = true;
  return true;
}

bool QrScanner::configure_camera() {
  return R_SUCCEEDED(CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A)) &&
         R_SUCCEEDED(CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A)) &&
         R_SUCCEEDED(CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30)) &&
         R_SUCCEEDED(CAMU_SetNoiseFilter(SELECT_OUT1, true)) &&
         R_SUCCEEDED(CAMU_SetAutoExposure(SELECT_OUT1, true)) &&
         R_SUCCEEDED(CAMU_SetAutoWhiteBalance(SELECT_OUT1, true)) &&
         R_SUCCEEDED(CAMU_Activate(SELECT_OUT1)) &&
         R_SUCCEEDED(CAMU_GetBufferErrorInterruptEvent(&buffer_error_event_, PORT_CAM1)) &&
         R_SUCCEEDED(CAMU_SetTrimming(PORT_CAM1, false)) &&
         R_SUCCEEDED(CAMU_GetMaxBytes(&transfer_unit_, kCameraWidth, kCameraHeight)) &&
         R_SUCCEEDED(
           CAMU_SetTransferBytes(PORT_CAM1, transfer_unit_, kCameraWidth, kCameraHeight)) &&
         R_SUCCEEDED(CAMU_ClearBuffer(PORT_CAM1));
}

bool QrScanner::queue_frame() {
  return R_SUCCEEDED(CAMU_SetReceiving(&receive_event_, camera_buffer_, PORT_CAM1,
                                       static_cast<std::uint32_t>(kCameraBufferSize),
                                       static_cast<std::int16_t>(transfer_unit_)));
}

bool QrScanner::recover_capture() {
  if (receive_event_ != 0) {
    svcCloseHandle(receive_event_);
    receive_event_ = 0;
  }
  return R_SUCCEEDED(CAMU_ClearBuffer(PORT_CAM1)) && queue_frame() &&
         R_SUCCEEDED(CAMU_StartCapture(PORT_CAM1));
}

QrScanUpdate QrScanner::update() {
  if (!active_) {
    return {QrScanStatus::failed, {}};
  }
  if (buffer_error_event_ != 0 && svcWaitSynchronization(buffer_error_event_, 0) == 0) {
    if (!recover_capture()) {
      stop();
      return {QrScanStatus::failed, {}};
    }
    return {};
  }
  if (receive_event_ == 0 || svcWaitSynchronization(receive_event_, 0) != 0) {
    return {};
  }
  svcCloseHandle(receive_event_);
  receive_event_ = 0;
  update_preview();
  auto payload = decode_frame();
  if (!payload.empty()) {
    stop();
    return {QrScanStatus::decoded, std::move(payload)};
  }
  if (!queue_frame()) {
    stop();
    return {QrScanStatus::failed, {}};
  }
  return {};
}

void QrScanner::update_preview() {
  auto *texture = static_cast<std::uint16_t *>(preview_texture_.data);
  for (int y = 0; y < kCameraHeight; ++y) {
    for (int x = 0; x < kCameraWidth; ++x) {
      texture[tiled_texture_offset(x, y)] = camera_buffer_[y * kCameraWidth + x];
    }
  }
  GSPGPU_FlushDataCache(preview_texture_.data, kTextureBufferSize);
  preview_ready_ = true;
}

std::string QrScanner::decode_frame() {
  int width = 0;
  int height = 0;
  auto *image = quirc_begin(decoder_, &width, &height);
  if (image == nullptr || width != kCameraWidth || height != kCameraHeight) {
    if (image != nullptr) {
      quirc_end(decoder_);
    }
    return {};
  }
  std::transform(camera_buffer_, camera_buffer_ + kCameraPixelCount, image, grayscale);
  quirc_end(decoder_);
  const auto count = quirc_count(decoder_);
  for (int index = 0; index < count; ++index) {
    quirc_code code{};
    quirc_data data{};
    quirc_extract(decoder_, index, &code);
    auto result = quirc_decode(&code, &data);
    if (result != QUIRC_SUCCESS) {
      quirc_flip(&code);
      result = quirc_decode(&code, &data);
    }
    if (result == QUIRC_SUCCESS && data.payload_len > 0) {
      return {reinterpret_cast<const char *>(data.payload),
              static_cast<std::size_t>(data.payload_len)};
    }
  }
  return {};
}

void QrScanner::stop() noexcept {
  active_ = false;
  if (camera_initialized_) {
    if (capturing_) {
      CAMU_StopCapture(PORT_CAM1);
      capturing_ = false;
    }
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
  }
  if (receive_event_ != 0) {
    svcCloseHandle(receive_event_);
    receive_event_ = 0;
  }
  if (buffer_error_event_ != 0) {
    svcCloseHandle(buffer_error_event_);
    buffer_error_event_ = 0;
  }
  if (camera_initialized_) {
    camExit();
    camera_initialized_ = false;
  }
  if (camera_buffer_ != nullptr) {
    linearFree(camera_buffer_);
    camera_buffer_ = nullptr;
  }
  if (decoder_ != nullptr) {
    quirc_destroy(decoder_);
    decoder_ = nullptr;
  }
  if (texture_initialized_) {
    C3D_TexDelete(&preview_texture_);
    texture_initialized_ = false;
  }
  transfer_unit_ = 0;
  preview_ready_ = false;
}

bool QrScanner::active() const noexcept { return active_; }

bool QrScanner::preview_ready() const noexcept { return preview_ready_; }

C3D_Tex *QrScanner::preview_texture() noexcept {
  return texture_initialized_ ? &preview_texture_ : nullptr;
}

} // namespace office3ds::platform_3ds
