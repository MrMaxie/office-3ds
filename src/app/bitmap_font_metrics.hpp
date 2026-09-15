#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace office3ds::app::font_metrics {

inline constexpr std::uint16_t kCellWidth = 10;
inline constexpr std::uint16_t kCellHeight = 16;
inline constexpr std::uint16_t kColumns = 32;
inline constexpr std::array<std::uint8_t, 113> kAdvances{
  2, 2, 4, 6, 6, 6, 7, 2, 3, 3, 5, 6, 2, 6, 2, 6, 6, 4, 6, 6, 6, 6, 6, 6, 6, 6, 2, 2, 5,
  6, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 4, 6, 5, 8, 6, 6, 6, 6, 6, 6, 6, 6, 6, 8, 6, 6,
  6, 3, 6, 3, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 2, 4, 6, 5, 8, 6, 6, 6, 6, 6, 6, 5, 6, 6,
  8, 6, 6, 6, 4, 2, 4, 7, 6, 6, 6, 5, 6, 6, 6, 6, 6, 6, 6, 6, 5, 6, 6, 6, 6, 6,
};
inline constexpr std::string_view kGlyphs = u8" !\"#$%&'()*+,-./"
                                            u8"0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
                                            u8"abcdefghijklmnopqrstuvwxyz{|}~ąćęłńóśźżĄĆĘŁŃÓŚŹŻ";

} // namespace office3ds::app::font_metrics
