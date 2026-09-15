#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace office3ds::demo::detail {

std::array<std::uint8_t, 32> sha256(std::string_view input);
std::string hexEncode(const std::array<std::uint8_t, 32> &input);

} // namespace office3ds::demo::detail
