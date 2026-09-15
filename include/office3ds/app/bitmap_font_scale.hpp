#pragma once

#include <cstdint>

namespace office3ds::app {

enum class BitmapFontScale : std::uint8_t {
  x1 = 1,
  x2 = 2,
  x3 = 3,
};

[[nodiscard]] constexpr float scale_factor(BitmapFontScale scale) noexcept {
  return static_cast<float>(scale);
}

[[nodiscard]] constexpr BitmapFontScale
fit_bitmap_font_scale(float width_at_x1, float maximum_width,
                      BitmapFontScale maximum_scale) noexcept {
  if (maximum_scale == BitmapFontScale::x3 && width_at_x1 * 3.0F <= maximum_width) {
    return BitmapFontScale::x3;
  }
  if (maximum_scale != BitmapFontScale::x1 && width_at_x1 * 2.0F <= maximum_width) {
    return BitmapFontScale::x2;
  }
  return BitmapFontScale::x1;
}

} // namespace office3ds::app
