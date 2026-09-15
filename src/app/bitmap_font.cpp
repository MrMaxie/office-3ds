#include "office3ds/app/bitmap_font.hpp"

#include "bitmap_font_metrics.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace office3ds::app {
namespace {

std::optional<std::uint32_t> next_codepoint(std::string_view text, std::size_t &offset) {
  if (offset >= text.size()) {
    return std::nullopt;
  }
  const auto first = static_cast<std::uint8_t>(text[offset++]);
  if (first < 0x80U) {
    return first;
  }
  if ((first & 0xE0U) == 0xC0U && offset < text.size()) {
    const auto second = static_cast<std::uint8_t>(text[offset++]);
    if ((second & 0xC0U) == 0x80U) {
      return static_cast<std::uint32_t>((first & 0x1FU) << 6U) | (second & 0x3FU);
    }
  } else if ((first & 0xF0U) == 0xE0U && offset + 1U < text.size()) {
    const auto second = static_cast<std::uint8_t>(text[offset++]);
    const auto third = static_cast<std::uint8_t>(text[offset++]);
    if ((second & 0xC0U) == 0x80U && (third & 0xC0U) == 0x80U) {
      return static_cast<std::uint32_t>((first & 0x0FU) << 12U) |
             static_cast<std::uint32_t>((second & 0x3FU) << 6U) | (third & 0x3FU);
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> glyph_index(std::uint32_t codepoint) {
  std::size_t offset = 0;
  std::size_t index = 0;
  while (offset < font_metrics::kGlyphs.size()) {
    const auto glyph = next_codepoint(font_metrics::kGlyphs, offset);
    if (glyph.has_value() && *glyph == codepoint) {
      return index;
    }
    ++index;
  }
  return std::nullopt;
}

} // namespace

bool BitmapFont::initialize() {
  atlas_ = C2D_SpriteSheetLoad("romfs:/kenney_pixel_font.t3x");
  if (atlas_ == nullptr) {
    return false;
  }
  C3D_TexSetFilter(C2D_SpriteSheetGetImage(atlas_, 0).tex, GPU_NEAREST, GPU_NEAREST);
  return true;
}

void BitmapFont::shutdown() noexcept {
  if (atlas_ != nullptr) {
    C2D_SpriteSheetFree(atlas_);
    atlas_ = nullptr;
  }
}

float BitmapFont::measure(std::string_view text, BitmapFontScale scale) const {
  float width = 0.0F;
  const auto factor = scale_factor(scale);
  std::size_t offset = 0;
  while (offset < text.size()) {
    const auto codepoint = next_codepoint(text, offset);
    const auto glyph = codepoint.has_value() ? glyph_index(*codepoint) : std::nullopt;
    if (glyph.has_value()) {
      width += static_cast<float>(font_metrics::kAdvances[*glyph]) * factor;
    }
  }
  return width;
}

float BitmapFont::line_height(BitmapFontScale scale) const {
  return static_cast<float>(font_metrics::kCellHeight) * scale_factor(scale);
}

void BitmapFont::draw(std::string_view text, float x, float y, BitmapFontScale scale, u32 alignment,
                      float depth, u32 color) const {
  if (atlas_ == nullptr) {
    return;
  }
  if ((alignment & C2D_AlignCenter) != 0U) {
    x -= measure(text, scale) / 2.0F;
  } else if ((alignment & C2D_AlignRight) != 0U) {
    x -= measure(text, scale);
  }

  const auto atlas_image = C2D_SpriteSheetGetImage(atlas_, 0);
  const auto factor = scale_factor(scale);
  const auto &atlas = *atlas_image.subtex;
  const auto u_per_pixel = (atlas.right - atlas.left) / static_cast<float>(atlas.width);
  const auto v_per_pixel = (atlas.top - atlas.bottom) / static_cast<float>(atlas.height);
  C2D_ImageTint tint{};
  C2D_PlainImageTint(&tint, color, 1.0F);

  std::size_t offset = 0;
  while (offset < text.size()) {
    const auto codepoint = next_codepoint(text, offset);
    const auto glyph = codepoint.has_value() ? glyph_index(*codepoint) : std::nullopt;
    if (!glyph.has_value()) {
      continue;
    }
    const auto column = *glyph % font_metrics::kColumns;
    const auto row = *glyph / font_metrics::kColumns;
    Tex3DS_SubTexture subtexture{
      font_metrics::kCellWidth,
      font_metrics::kCellHeight,
      atlas.left + static_cast<float>(column * font_metrics::kCellWidth) * u_per_pixel,
      atlas.top - static_cast<float>(row * font_metrics::kCellHeight) * v_per_pixel,
      atlas.left + static_cast<float>((column + 1U) * font_metrics::kCellWidth) * u_per_pixel,
      atlas.top - static_cast<float>((row + 1U) * font_metrics::kCellHeight) * v_per_pixel,
    };
    const C2D_Image image{atlas_image.tex, &subtexture};
    C2D_DrawImageAt(image, x, y, depth, &tint, factor, factor);
    x += static_cast<float>(font_metrics::kAdvances[*glyph]) * factor;
  }
}

} // namespace office3ds::app
