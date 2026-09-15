#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <citro2d.h>
#include <citro3d.h>

#include "office3ds/api/models.hpp"
#include "office3ds/app/avatar_images.hpp"
#include "office3ds/platform_3ds/avatar_cache_store.hpp"
#include "office3ds/platform_3ds/http.hpp"

namespace office3ds::platform_3ds {

class AvatarStore : public app::AvatarImages {
public:
  explicit AvatarStore(std::string product_slug);
  bool initialize(const HttpRuntime &runtime);
  void shutdown();

  void load(const api::DashboardSnapshot &snapshot);
  void clear();
  void update();
  [[nodiscard]] const C2D_Image *find(std::string_view url) const override;

private:
  struct AvatarTexture {
    std::string url;
    C3D_Tex texture{};
    Tex3DS_SubTexture subtexture{};
    C2D_Image image{};
  };

  struct PendingAvatar {
    std::string url;
    std::size_t generation = 0;
    std::size_t maximum_response_size = 0;
  };

  struct PreparedAvatar {
    std::string url;
    std::vector<std::uint8_t> rgba;
    unsigned int size = 0;
    std::size_t generation = 0;
  };

  void retainSnapshotTextures(const std::vector<std::string> &urls);
  static void workerEntry(void *context);
  void workerLoop();
  [[nodiscard]] bool upload(std::string url, const std::vector<std::uint8_t> &rgba,
                            unsigned int size, bool update_cache);
  void saveCacheIfComplete();

  bool initialized_ = false;
  AvatarCacheStore cache_store_;
  std::vector<CachedAvatar> cached_avatars_;
  bool cache_dirty_ = false;
  std::vector<AvatarTexture> textures_;
  std::array<Thread, 2> workers_{};
  LightLock queue_lock_{};
  LightLock decode_lock_{};
  LightSemaphore work_semaphore_{};
  bool stop_requested_ = false;
  std::size_t busy_workers_ = 0;
  std::size_t generation_ = 0;
  std::deque<PendingAvatar> pending_avatars_;
  std::deque<PreparedAvatar> prepared_avatars_;
};

} // namespace office3ds::platform_3ds
