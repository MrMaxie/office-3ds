#pragma once

#include <cstddef>
#include <string_view>

namespace office3ds::core {

inline constexpr std::size_t kMaximumListAvatarResponseSize = 192U * 1024U;
inline constexpr std::size_t kMaximumProfileAvatarResponseSize = 512U * 1024U;
inline constexpr std::size_t kMaximumConcurrentAvatarResponses = 2U;
inline constexpr std::size_t kPreferredAvatarEdge = 32U;
inline constexpr std::size_t kDegradedAvatarEdge = 16U;

[[nodiscard]] constexpr std::size_t maximum_avatar_response_size(std::string_view profile_url,
                                                                 std::string_view candidate_url) {
  return !profile_url.empty() && candidate_url == profile_url ? kMaximumProfileAvatarResponseSize
                                                              : kMaximumListAvatarResponseSize;
}

} // namespace office3ds::core
