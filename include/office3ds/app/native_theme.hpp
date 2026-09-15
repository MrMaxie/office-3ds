#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace office3ds::app {

struct NativeTheme {
  std::uint32_t background = 0xFF15191DU;
  std::uint32_t surface = 0xFF292F35U;
  std::uint32_t surface_high = 0xFF39434BU;
  std::uint32_t track = 0xFF1B2024U;
  std::uint32_t muted = 0xFF8C969EU;
  std::uint32_t primary = 0xFF976F2AU;
  std::uint32_t secondary = 0xFFD69E2EU;
  std::uint32_t success = 0xFF78B945U;
  std::uint32_t text = 0xFFF7FAFCU;
  std::uint32_t border = 0xFF101214U;
};

struct NativeCopy {
  std::string product_name{"Office"};
  std::string worklog{"Worklog"};
  std::string absences{"Absences"};
  std::string activity{"Activity"};
  std::string recognition{"Recognition"};
};

[[nodiscard]] std::uint32_t parse_rgba(std::string_view value, std::uint32_t fallback) noexcept;

} // namespace office3ds::app
