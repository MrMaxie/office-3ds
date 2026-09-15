#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <3ds.h>

namespace office3ds::platform_3ds {

struct AvatarCacheKey {
  std::uint64_t primary = 0;
  std::uint64_t secondary = 0;

  bool operator==(const AvatarCacheKey &other) const {
    return primary == other.primary && secondary == other.secondary;
  }
};

struct CachedAvatar {
  AvatarCacheKey key;
  std::int64_t stored_at = 0;
  std::uint32_t size = 0;
  std::vector<std::uint8_t> rgba;
};

[[nodiscard]] AvatarCacheKey avatarCacheKey(std::string_view url);

class AvatarCacheStore {
public:
  explicit AvatarCacheStore(std::string product_slug);
  bool initialize();
  void shutdown();

  [[nodiscard]] std::vector<CachedAvatar> load(std::int64_t now_epoch_seconds) const;
  bool save(const std::vector<CachedAvatar> &avatars) const;

private:
  std::string directory_;
  std::string cache_path_;
  std::string temporary_path_;
  std::string backup_path_;
  bool initialized_ = false;
  FS_Archive archive_{};
};

} // namespace office3ds::platform_3ds
