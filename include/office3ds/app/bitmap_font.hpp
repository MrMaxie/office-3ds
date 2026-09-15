#pragma once

#include <string_view>

#include <citro2d.h>

#include "office3ds/app/bitmap_font_scale.hpp"

namespace office3ds::app {

class BitmapFont {
public:
  [[nodiscard]] bool initialize();
  void shutdown() noexcept;

  [[nodiscard]] float measure(std::string_view text,
                              BitmapFontScale scale = BitmapFontScale::x2) const;
  [[nodiscard]] float line_height(BitmapFontScale scale = BitmapFontScale::x2) const;
  void draw(std::string_view text, float x, float y, BitmapFontScale scale = BitmapFontScale::x2,
            u32 alignment = C2D_AlignLeft, float depth = 0.5F,
            u32 color = C2D_Color32(255, 255, 255, 255)) const;

private:
  C2D_SpriteSheet atlas_{};
};

} // namespace office3ds::app
