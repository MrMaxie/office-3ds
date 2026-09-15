#include "office3ds/platform_3ds/avatar_store.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <3ds.h>
#include <png.h>
#include <turbojpeg.h>

#include "office3ds/core/avatar_policy.hpp"
#include "office3ds/platform_3ds/curl_download.hpp"

namespace office3ds::platform_3ds {
namespace {

constexpr auto kTextureSize = 32U;
constexpr auto kMaximumAvatarCount = std::size_t{24};
constexpr auto kDownloadWorkerCount = std::size_t{2};
constexpr auto kMaximumPreparedAvatars = std::size_t{2};
constexpr auto kMaximumSourceDimension = 2048U;
constexpr auto kPreferredAvatarSize = 32U;
constexpr auto kReducedAvatarSize = 16U;
constexpr auto kMaximumScaledDecodeBytes = std::size_t{512U * 1024U};

struct DownloadedImage {
  std::string content_type;
  std::vector<std::uint8_t> body;
};

struct DecodedImage {
  // cppcheck-suppress unusedStructMember
  std::vector<std::uint8_t> rgba;
  unsigned int size = 0;
};

struct PngMemoryReader {
  const std::uint8_t *data = nullptr;
  std::size_t size = 0;
  std::size_t offset = 0;
};

void readPngBytes(png_structp png, png_bytep target, png_size_t size) {
  auto *reader = static_cast<PngMemoryReader *>(png_get_io_ptr(png));
  if (reader == nullptr || size > reader->size - reader->offset) {
    png_error(png, "Avatar PNG ended unexpectedly.");
    return;
  }
  std::memcpy(target, reader->data + reader->offset, size);
  reader->offset += size;
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

bool validAvatarUrl(std::string_view url) {
  return url.size() <= 2048 && url.rfind("https://", 0) == 0;
}

std::optional<DownloadedImage> downloadAvatar(std::string_view url,
                                              std::size_t maximum_response_size) {
  if (!validAvatarUrl(url) || maximum_response_size == 0) {
    return std::nullopt;
  }
  CurlRequest request{
    std::string(url), {"Accept: image/png,image/jpeg"}, {}, maximum_response_size, false, true};
  std::vector<std::uint8_t> body;
  CurlResponse response;
  if (!performCurlRequest(request, body, response) || response.status_code < 200 ||
      response.status_code >= 300 || body.empty()) {
    return std::nullopt;
  }
  return DownloadedImage{lowercase(std::move(response.content_type)), std::move(body)};
}

std::vector<std::uint8_t> resizeSquare(const std::vector<std::uint8_t> &source,
                                       unsigned int source_width, unsigned int source_height,
                                       unsigned int target_size) {
  std::vector<std::uint8_t> output(target_size * target_size * 4U);
  const auto crop_size = std::min(source_width, source_height);
  const auto crop_x = (source_width - crop_size) / 2U;
  const auto crop_y = (source_height - crop_size) / 2U;
  for (auto y = 0U; y < target_size; ++y) {
    for (auto x = 0U; x < target_size; ++x) {
      const auto source_x = crop_x + std::min(crop_size - 1U, x * crop_size / target_size);
      const auto source_y = crop_y + std::min(crop_size - 1U, y * crop_size / target_size);
      const auto source_offset =
        (static_cast<std::size_t>(source_y) * source_width + source_x) * 4U;
      const auto target_offset = (static_cast<std::size_t>(y) * target_size + x) * 4U;
      std::copy_n(source.data() + source_offset, 4, output.data() + target_offset);
    }
  }
  return output;
}

std::optional<DecodedImage> decodePng(const std::vector<std::uint8_t> &body,
                                      unsigned int target_size) {
  if (body.size() < 8U || png_sig_cmp(body.data(), 0, 8) != 0) {
    return std::nullopt;
  }
  auto *png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  auto *info = png == nullptr ? nullptr : png_create_info_struct(png);
  if (png == nullptr || info == nullptr) {
    png_destroy_read_struct(&png, info == nullptr ? nullptr : &info, nullptr);
    return std::nullopt;
  }
  PngMemoryReader reader{body.data(), body.size(), 8U};
  if (setjmp(png_jmpbuf(png)) != 0) {
    png_destroy_read_struct(&png, &info, nullptr);
    return std::nullopt;
  }
  png_set_read_fn(png, &reader, readPngBytes);
  png_set_sig_bytes(png, 8);
  png_read_info(png, info);
  const auto width = png_get_image_width(png, info);
  const auto height = png_get_image_height(png, info);
  const auto color_type = png_get_color_type(png, info);
  const auto bit_depth = png_get_bit_depth(png, info);
  if (width == 0 || height == 0 || width > kMaximumSourceDimension ||
      height > kMaximumSourceDimension || png_get_interlace_type(png, info) != PNG_INTERLACE_NONE) {
    png_destroy_read_struct(&png, &info, nullptr);
    return std::nullopt;
  }
  if (bit_depth == 16) {
    png_set_strip_16(png);
  }
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }
  if (png_get_valid(png, info, PNG_INFO_tRNS) != 0) {
    png_set_tRNS_to_alpha(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png);
  }
  if ((color_type & PNG_COLOR_MASK_ALPHA) == 0 && png_get_valid(png, info, PNG_INFO_tRNS) == 0) {
    png_set_add_alpha(png, 0xFFU, PNG_FILLER_AFTER);
  }
  png_read_update_info(png, info);
  if (png_get_rowbytes(png, info) != static_cast<png_size_t>(width) * 4U) {
    png_destroy_read_struct(&png, &info, nullptr);
    return std::nullopt;
  }

  std::vector<std::uint8_t> output(target_size * target_size * 4U);
  std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 4U);
  const auto crop_size = std::min(width, height);
  const auto crop_x = (width - crop_size) / 2U;
  const auto crop_y = (height - crop_size) / 2U;
  for (auto source_y = 0U; source_y < height; ++source_y) {
    png_read_row(png, row.data(), nullptr);
    for (auto target_y = 0U; target_y < target_size; ++target_y) {
      if (source_y != crop_y + std::min(crop_size - 1U, target_y * crop_size / target_size)) {
        continue;
      }
      for (auto target_x = 0U; target_x < target_size; ++target_x) {
        const auto source_x = crop_x + std::min(crop_size - 1U, target_x * crop_size / target_size);
        std::copy_n(row.data() + static_cast<std::size_t>(source_x) * 4U, 4,
                    output.data() +
                      (static_cast<std::size_t>(target_y) * target_size + target_x) * 4U);
      }
    }
  }
  png_read_end(png, nullptr);
  png_destroy_read_struct(&png, &info, nullptr);
  return DecodedImage{std::move(output), target_size};
}

std::optional<DecodedImage> decodeJpeg(const std::vector<std::uint8_t> &body,
                                       unsigned int target_size) {
  if (body.empty()) {
    return std::nullopt;
  }
  auto *decoder = tjInitDecompress();
  if (decoder == nullptr) {
    return std::nullopt;
  }
  int width = 0;
  int height = 0;
  int subsampling = 0;
  int colorSpace = 0;
  if (tjDecompressHeader3(decoder, body.data(), body.size(), &width, &height, &subsampling,
                          &colorSpace) != 0 ||
      width <= 0 || height <= 0 || width > static_cast<int>(kMaximumSourceDimension) ||
      height > static_cast<int>(kMaximumSourceDimension)) {
    tjDestroy(decoder);
    return std::nullopt;
  }
  int factor_count = 0;
  const auto *factors = tjGetScalingFactors(&factor_count);
  tjscalingfactor selected{1, 1};
  for (auto index = 0; index < factor_count; ++index) {
    const auto scaled_width = TJSCALED(width, factors[index]);
    const auto scaled_height = TJSCALED(height, factors[index]);
    if (scaled_width >= static_cast<int>(target_size) &&
        scaled_height >= static_cast<int>(target_size) &&
        static_cast<long long>(scaled_width) * scaled_height <
          static_cast<long long>(TJSCALED(width, selected)) * TJSCALED(height, selected)) {
      selected = factors[index];
    }
  }
  const auto scaled_width = TJSCALED(width, selected);
  const auto scaled_height = TJSCALED(height, selected);
  const auto scaled_bytes = static_cast<std::size_t>(scaled_width) * scaled_height * 4U;
  if (scaled_bytes > kMaximumScaledDecodeBytes) {
    tjDestroy(decoder);
    return std::nullopt;
  }
  std::vector<std::uint8_t> rgba(scaled_bytes);
  if (tjDecompress2(decoder, body.data(), body.size(), rgba.data(), scaled_width, 0, scaled_height,
                    TJPF_RGBA, TJFLAG_FASTDCT) != 0) {
    tjDestroy(decoder);
    return std::nullopt;
  }
  tjDestroy(decoder);
  auto reduced = resizeSquare(rgba, static_cast<unsigned int>(scaled_width),
                              static_cast<unsigned int>(scaled_height), target_size);
  rgba.clear();
  rgba.shrink_to_fit();
  return DecodedImage{std::move(reduced), target_size};
}

std::optional<DecodedImage> decode(const DownloadedImage &download, unsigned int target_size) {
  if (download.content_type.find("png") != std::string::npos ||
      (download.body.size() >= 8 && png_sig_cmp(download.body.data(), 0, 8) == 0)) {
    return decodePng(download.body, target_size);
  }
  if (download.content_type.find("jpeg") != std::string::npos ||
      download.content_type.find("jpg") != std::string::npos ||
      (download.body.size() >= 2 && download.body[0] == 0xFF && download.body[1] == 0xD8)) {
    return decodeJpeg(download.body, target_size);
  }
  return std::nullopt;
}

std::size_t tiledTextureOffset(unsigned int x, unsigned int y) {
  return static_cast<std::size_t>((((y >> 3U) * (kTextureSize >> 3U) + (x >> 3U)) << 6U) +
                                  ((x & 1U) | ((y & 1U) << 1U) | ((x & 2U) << 1U) |
                                   ((y & 2U) << 2U) | ((x & 4U) << 2U) | ((y & 4U) << 3U)));
}

} // namespace

bool AvatarStore::initialize(const HttpRuntime &runtime) {
  initialized_ = runtime.ready() && cache_store_.initialize();
  if (!initialized_) {
    return false;
  }
  cached_avatars_ = cache_store_.load(static_cast<std::int64_t>(std::time(nullptr)));
  textures_.reserve(kMaximumAvatarCount);
  LightLock_Init(&queue_lock_);
  LightLock_Init(&decode_lock_);
  LightSemaphore_Init(&work_semaphore_, 0, 8);
  for (auto index = std::size_t{0}; index < workers_.size(); ++index) {
    workers_[index] = threadCreate(workerEntry, this, 64U * 1024U, 0x31, -2, false);
    if (workers_[index] == nullptr) {
      LightLock_Lock(&queue_lock_);
      stop_requested_ = true;
      LightLock_Unlock(&queue_lock_);
      if (index > 0) {
        LightSemaphore_Release(&work_semaphore_, static_cast<s32>(index));
      }
      for (auto joined = std::size_t{0}; joined < index; ++joined) {
        (void)threadJoin(workers_[joined], U64_MAX);
        threadFree(workers_[joined]);
        workers_[joined] = nullptr;
      }
      cache_store_.shutdown();
      initialized_ = false;
      return false;
    }
  }
  return true;
}

void AvatarStore::shutdown() {
  if (!initialized_ && std::all_of(workers_.begin(), workers_.end(),
                                   [](Thread worker) { return worker == nullptr; })) {
    return;
  }
  LightLock_Lock(&queue_lock_);
  stop_requested_ = true;
  pending_avatars_.clear();
  LightLock_Unlock(&queue_lock_);
  LightSemaphore_Release(&work_semaphore_, static_cast<s32>(workers_.size()));
  for (auto &worker : workers_) {
    if (worker != nullptr) {
      (void)threadJoin(worker, U64_MAX);
      threadFree(worker);
      worker = nullptr;
    }
  }
  clear();
  cache_store_.shutdown();
  initialized_ = false;
}

void AvatarStore::clear() {
  LightLock_Lock(&queue_lock_);
  pending_avatars_.clear();
  prepared_avatars_.clear();
  LightLock_Unlock(&queue_lock_);
  for (auto &texture : textures_) {
    C3D_TexDelete(&texture.texture);
  }
  textures_.clear();
}

AvatarStore::AvatarStore(std::string product_slug) : cache_store_(std::move(product_slug)) {}

void AvatarStore::load(const api::DashboardSnapshot &snapshot) {
  if (!initialized_) {
    return;
  }
  std::vector<std::string> urls;
  urls.push_back(snapshot.profile.avatar_url);
  std::transform(snapshot.absences.begin(), snapshot.absences.end(), std::back_inserter(urls),
                 [](const auto &person) { return person.avatar_url; });
  std::transform(snapshot.activity.begin(), snapshot.activity.end(), std::back_inserter(urls),
                 [](const auto &event) { return event.avatar_url; });
  urls.erase(
    std::remove_if(urls.begin(), urls.end(), [](const auto &url) { return !validAvatarUrl(url); }),
    urls.end());
  std::vector<std::string> unique_urls;
  unique_urls.reserve(std::min(urls.size(), kMaximumAvatarCount));
  for (auto &url : urls) {
    if (unique_urls.size() >= kMaximumAvatarCount) {
      break;
    }
    if (std::find(unique_urls.begin(), unique_urls.end(), url) == unique_urls.end()) {
      unique_urls.push_back(std::move(url));
    }
  }
  retainSnapshotTextures(unique_urls);
  const auto cache_size_before = cached_avatars_.size();
  cached_avatars_.erase(std::remove_if(cached_avatars_.begin(), cached_avatars_.end(),
                                       [&unique_urls](const auto &entry) {
                                         return std::none_of(unique_urls.begin(), unique_urls.end(),
                                                             [&entry](const auto &url) {
                                                               return avatarCacheKey(url) ==
                                                                      entry.key;
                                                             });
                                       }),
                        cached_avatars_.end());
  cache_dirty_ = cache_dirty_ || cached_avatars_.size() != cache_size_before;
  LightLock_Lock(&queue_lock_);
  ++generation_;
  pending_avatars_.clear();
  prepared_avatars_.clear();
  for (const auto &url : unique_urls) {
    if (find(url) == nullptr) {
      const auto key = avatarCacheKey(url);
      const auto cached = std::find_if(cached_avatars_.begin(), cached_avatars_.end(),
                                       [&key](const auto &entry) { return entry.key == key; });
      if (cached != cached_avatars_.end() && upload(url, cached->rgba, cached->size, false)) {
        continue;
      }
      const auto maximum_response_size =
        core::maximum_avatar_response_size(snapshot.profile.avatar_url, url);
      pending_avatars_.push_back({url, generation_, maximum_response_size});
    }
  }
  const auto initial_jobs = std::min(kDownloadWorkerCount, pending_avatars_.size());
  LightLock_Unlock(&queue_lock_);
  if (initial_jobs > 0) {
    LightSemaphore_Release(&work_semaphore_, static_cast<s32>(initial_jobs));
  }
  saveCacheIfComplete();
}

void AvatarStore::update() {
  if (!initialized_) {
    return;
  }
  std::optional<PreparedAvatar> avatar;
  LightLock_Lock(&queue_lock_);
  if (!prepared_avatars_.empty()) {
    avatar = std::move(prepared_avatars_.front());
    prepared_avatars_.pop_front();
  }
  LightLock_Unlock(&queue_lock_);
  if (avatar.has_value() && avatar->generation == generation_) {
    (void)upload(std::move(avatar->url), avatar->rgba, avatar->size, true);
  }
  LightLock_Lock(&queue_lock_);
  const auto schedule_next = prepared_avatars_.size() + busy_workers_ < kMaximumPreparedAvatars &&
                             !pending_avatars_.empty() && busy_workers_ < kDownloadWorkerCount;
  LightLock_Unlock(&queue_lock_);
  if (schedule_next) {
    LightSemaphore_Release(&work_semaphore_, 1);
  }
  saveCacheIfComplete();
}

void AvatarStore::retainSnapshotTextures(const std::vector<std::string> &urls) {
  auto iterator = textures_.begin();
  while (iterator != textures_.end()) {
    if (std::find(urls.begin(), urls.end(), iterator->url) == urls.end()) {
      C3D_TexDelete(&iterator->texture);
      iterator = textures_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  for (auto &texture : textures_) {
    texture.image = {&texture.texture, &texture.subtexture};
  }
}

const C2D_Image *AvatarStore::find(std::string_view url) const {
  const auto found = std::find_if(textures_.begin(), textures_.end(),
                                  [url](const auto &entry) { return entry.url == url; });
  return found == textures_.end() ? nullptr : &found->image;
}

void AvatarStore::workerEntry(void *context) { static_cast<AvatarStore *>(context)->workerLoop(); }

void AvatarStore::workerLoop() {
  while (true) {
    LightSemaphore_Acquire(&work_semaphore_, 1);
    PendingAvatar pending;
    LightLock_Lock(&queue_lock_);
    if (stop_requested_) {
      LightLock_Unlock(&queue_lock_);
      return;
    }
    if (prepared_avatars_.size() >= kMaximumPreparedAvatars || pending_avatars_.empty()) {
      LightLock_Unlock(&queue_lock_);
      continue;
    }
    pending = std::move(pending_avatars_.front());
    pending_avatars_.pop_front();
    ++busy_workers_;
    LightLock_Unlock(&queue_lock_);

    std::optional<DecodedImage> image;
    auto download = downloadAvatar(pending.url, pending.maximum_response_size);
    if (download.has_value()) {
      LightLock_Lock(&decode_lock_);
      image = decode(*download, kPreferredAvatarSize);
      if (!image.has_value()) {
        image = decode(*download, kReducedAvatarSize);
      }
      LightLock_Unlock(&decode_lock_);
      download.reset();
    }

    LightLock_Lock(&queue_lock_);
    --busy_workers_;
    if (image.has_value() && pending.generation == generation_ &&
        prepared_avatars_.size() < kMaximumPreparedAvatars) {
      prepared_avatars_.push_back(PreparedAvatar{std::move(pending.url), std::move(image->rgba),
                                                 image->size, pending.generation});
    }
    const auto continue_loading =
      prepared_avatars_.size() + busy_workers_ < kMaximumPreparedAvatars &&
      !pending_avatars_.empty() && busy_workers_ < kDownloadWorkerCount;
    LightLock_Unlock(&queue_lock_);
    if (continue_loading) {
      LightSemaphore_Release(&work_semaphore_, 1);
    }
  }
}

bool AvatarStore::upload(std::string url, const std::vector<std::uint8_t> &rgba, unsigned int size,
                         bool update_cache) {
  if ((size != kPreferredAvatarSize && size != kReducedAvatarSize) ||
      rgba.size() != static_cast<std::size_t>(size) * size * 4U) {
    return false;
  }
  AvatarTexture entry;
  if (!C3D_TexInit(&entry.texture, kTextureSize, kTextureSize, GPU_RGBA8)) {
    return false;
  }
  C3D_TexSetFilter(&entry.texture, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetWrap(&entry.texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
  auto *target = static_cast<std::uint32_t *>(entry.texture.data);
  std::memset(entry.texture.data, 0, kTextureSize * kTextureSize * 4U);
  for (auto y = 0U; y < size; ++y) {
    for (auto x = 0U; x < size; ++x) {
      const auto source = (static_cast<std::size_t>(y) * size + x) * 4U;
      target[tiledTextureOffset(x, y)] = static_cast<std::uint32_t>(rgba[source + 3U]) |
                                         (static_cast<std::uint32_t>(rgba[source + 2U]) << 8U) |
                                         (static_cast<std::uint32_t>(rgba[source + 1U]) << 16U) |
                                         (static_cast<std::uint32_t>(rgba[source]) << 24U);
    }
  }
  GSPGPU_FlushDataCache(entry.texture.data, kTextureSize * kTextureSize * 4U);
  entry.url = std::move(url);
  entry.subtexture = {static_cast<u16>(size),
                      static_cast<u16>(size),
                      0.0F,
                      static_cast<float>(size) / kTextureSize,
                      static_cast<float>(size) / kTextureSize,
                      0.0F};
  entry.image = {&entry.texture, &entry.subtexture};
  textures_.push_back(std::move(entry));
  auto &stored = textures_.back();
  stored.image = {&stored.texture, &stored.subtexture};
  if (update_cache) {
    const auto key = avatarCacheKey(stored.url);
    const auto existing = std::find_if(cached_avatars_.begin(), cached_avatars_.end(),
                                       [&key](const auto &entry) { return entry.key == key; });
    CachedAvatar cached{key, static_cast<std::int64_t>(std::time(nullptr)), size, rgba};
    if (existing == cached_avatars_.end()) {
      if (cached_avatars_.size() < kMaximumAvatarCount) {
        cached_avatars_.push_back(std::move(cached));
      }
    } else {
      *existing = std::move(cached);
    }
    cache_dirty_ = true;
  }
  return true;
}

void AvatarStore::saveCacheIfComplete() {
  LightLock_Lock(&queue_lock_);
  const auto complete = busy_workers_ == 0 && pending_avatars_.empty() && prepared_avatars_.empty();
  LightLock_Unlock(&queue_lock_);
  if (complete && cache_dirty_ && cache_store_.save(cached_avatars_)) {
    cache_dirty_ = false;
  }
}

} // namespace office3ds::platform_3ds
