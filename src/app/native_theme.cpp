#include "office3ds/app/native_theme.hpp"

#include <charconv>
#include <system_error>

namespace office3ds::app {

std::uint32_t parse_rgba(std::string_view value, std::uint32_t fallback) noexcept {
  if (value.size() != 7 || value.front() != '#') {
    return fallback;
  }
  std::uint32_t rgb = 0;
  const auto result = std::from_chars(value.data() + 1, value.data() + value.size(), rgb, 16);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    return fallback;
  }
  const auto red = (rgb >> 16U) & 0xFFU;
  const auto green = (rgb >> 8U) & 0xFFU;
  const auto blue = rgb & 0xFFU;
  return red | (green << 8U) | (blue << 16U) | 0xFF000000U;
}

} // namespace office3ds::app
