#include "office3ds/app/bitmap_font_scale.hpp"

#include <cassert>

int main() {
  using office3ds::app::BitmapFontScale;
  using office3ds::app::fit_bitmap_font_scale;

  assert(fit_bitmap_font_scale(20.0F, 60.0F, BitmapFontScale::x3) == BitmapFontScale::x3);
  assert(fit_bitmap_font_scale(21.0F, 60.0F, BitmapFontScale::x3) == BitmapFontScale::x2);
  assert(fit_bitmap_font_scale(31.0F, 60.0F, BitmapFontScale::x3) == BitmapFontScale::x1);
  assert(fit_bitmap_font_scale(20.0F, 60.0F, BitmapFontScale::x2) == BitmapFontScale::x2);
  assert(fit_bitmap_font_scale(20.0F, 60.0F, BitmapFontScale::x1) == BitmapFontScale::x1);
}
