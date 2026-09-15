#pragma once

#include <cstdint>

namespace office3ds::platform_3ds {

class AudioFeedback {
public:
  ~AudioFeedback();

  [[nodiscard]] bool initialize();
  void update() noexcept;
  void select() noexcept;
  void refresh() noexcept;
  void error() noexcept;
  void shutdown() noexcept;

private:
  void queue_buffer(unsigned int index) noexcept;
  void set_accent(float frequency, unsigned int frames) noexcept;

  std::int16_t *samples_ = nullptr;
  float phase_ = 0.0F;
  float accent_frequency_ = 196.0F;
  unsigned int accent_frames_ = 0;
  unsigned int next_buffer_ = 0;
  bool initialized_ = false;
};

} // namespace office3ds::platform_3ds
