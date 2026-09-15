#include "office3ds/platform_3ds/avatar_cache_store.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>

#include <monocypher.h>

namespace office3ds::platform_3ds {
namespace {

constexpr auto kMaximumCacheEntries = std::size_t{24};
constexpr auto kMaximumCacheFileSize = 128ULL * 1024ULL;
constexpr auto kMaximumCacheAgeSeconds = std::int64_t{30 * 24 * 60 * 60};
constexpr std::array<std::uint8_t, 8> kMagic{'O', '3', 'D', 'A', 'V', 'C', '0', '1'};

template <typename Value> void appendInteger(std::vector<std::uint8_t> &body, Value value) {
  for (std::size_t index = 0; index < sizeof(Value); ++index) {
    body.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
  }
}

template <typename Value>
std::optional<Value> readInteger(const std::vector<std::uint8_t> &body, std::size_t &offset) {
  if (offset > body.size() || body.size() - offset < sizeof(Value)) {
    return std::nullopt;
  }
  Value value = 0;
  for (std::size_t index = 0; index < sizeof(Value); ++index) {
    value |= static_cast<Value>(body[offset + index]) << (index * 8U);
  }
  offset += sizeof(Value);
  return value;
}

std::optional<std::vector<std::uint8_t>> readCacheFile(FS_Archive archive, const char *path) {
  Handle file = 0;
  if (R_FAILED(FSUSER_OpenFile(&file, archive, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0))) {
    return std::nullopt;
  }
  u64 size = 0;
  if (R_FAILED(FSFILE_GetSize(file, &size)) || size < kMagic.size() + sizeof(std::uint32_t) ||
      size > kMaximumCacheFileSize) {
    FSFILE_Close(file);
    return std::nullopt;
  }
  std::vector<std::uint8_t> body(static_cast<std::size_t>(size));
  u32 bytes_read = 0;
  const auto result = FSFILE_Read(file, &bytes_read, 0, body.data(), static_cast<u32>(body.size()));
  FSFILE_Close(file);
  return R_SUCCEEDED(result) && bytes_read == body.size()
           ? std::optional<std::vector<std::uint8_t>>{std::move(body)}
           : std::nullopt;
}

bool validRecord(const CachedAvatar &avatar) {
  return (avatar.size == 16U || avatar.size == 32U) &&
         avatar.rgba.size() == static_cast<std::size_t>(avatar.size) * avatar.size * 4U;
}

} // namespace

AvatarCacheStore::AvatarCacheStore(std::string product_slug)
    : directory_("/3ds/" + product_slug), cache_path_(directory_ + "/avatar-cache.bin"),
      temporary_path_(directory_ + "/avatar-cache.tmp"),
      backup_path_(directory_ + "/avatar-cache.bak") {}

AvatarCacheKey avatarCacheKey(std::string_view url) {
  std::array<std::uint8_t, 16> digest{};
  crypto_blake2b(digest.data(), digest.size(), reinterpret_cast<const std::uint8_t *>(url.data()),
                 url.size());
  auto primary = std::uint64_t{0};
  auto secondary = std::uint64_t{0};
  for (auto index = std::size_t{0}; index < sizeof(std::uint64_t); ++index) {
    primary |= static_cast<std::uint64_t>(digest[index]) << (index * 8U);
    secondary |= static_cast<std::uint64_t>(digest[index + sizeof(std::uint64_t)]) << (index * 8U);
  }
  crypto_wipe(digest.data(), digest.size());
  return {primary, secondary};
}

bool AvatarCacheStore::initialize() {
  if (initialized_) {
    return true;
  }
  if (R_FAILED(fsInit())) {
    return false;
  }
  if (R_FAILED(FSUSER_OpenArchive(&archive_, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
    fsExit();
    return false;
  }
  (void)FSUSER_CreateDirectory(archive_, fsMakePath(PATH_ASCII, "/3ds"), 0);
  (void)FSUSER_CreateDirectory(archive_, fsMakePath(PATH_ASCII, directory_.c_str()), 0);
  initialized_ = true;
  return true;
}

void AvatarCacheStore::shutdown() {
  if (!initialized_) {
    return;
  }
  FSUSER_CloseArchive(archive_);
  archive_ = {};
  fsExit();
  initialized_ = false;
}

std::vector<CachedAvatar> AvatarCacheStore::load(std::int64_t now_epoch_seconds) const {
  std::vector<CachedAvatar> avatars;
  if (!initialized_) {
    return avatars;
  }
  auto body = readCacheFile(archive_, cache_path_.c_str());
  if (!body.has_value()) {
    body = readCacheFile(archive_, backup_path_.c_str());
  }
  if (!body.has_value() || !std::equal(kMagic.begin(), kMagic.end(), body->begin())) {
    return avatars;
  }
  auto offset = kMagic.size();
  const auto count = readInteger<std::uint32_t>(*body, offset);
  if (!count.has_value() || *count > kMaximumCacheEntries) {
    return {};
  }
  avatars.reserve(*count);
  for (auto index = 0U; index < *count; ++index) {
    const auto primary = readInteger<std::uint64_t>(*body, offset);
    const auto secondary = readInteger<std::uint64_t>(*body, offset);
    const auto stored_at_raw = readInteger<std::uint64_t>(*body, offset);
    const auto size = readInteger<std::uint32_t>(*body, offset);
    if (!primary.has_value() || !secondary.has_value() || !stored_at_raw.has_value() ||
        !size.has_value() || (*size != 16U && *size != 32U)) {
      return {};
    }
    const auto rgba_size = static_cast<std::size_t>(*size) * *size * 4U;
    if (offset > body->size() || body->size() - offset < rgba_size ||
        *stored_at_raw > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return {};
    }
    const auto stored_at = static_cast<std::int64_t>(*stored_at_raw);
    if (stored_at <= now_epoch_seconds &&
        now_epoch_seconds - stored_at <= kMaximumCacheAgeSeconds) {
      avatars.push_back({{*primary, *secondary},
                         stored_at,
                         *size,
                         {body->begin() + static_cast<std::ptrdiff_t>(offset),
                          body->begin() + static_cast<std::ptrdiff_t>(offset + rgba_size)}});
    }
    offset += rgba_size;
  }
  return offset == body->size() ? avatars : std::vector<CachedAvatar>{};
}

bool AvatarCacheStore::save(const std::vector<CachedAvatar> &avatars) const {
  if (!initialized_ || avatars.size() > kMaximumCacheEntries ||
      !std::all_of(avatars.begin(), avatars.end(), validRecord)) {
    return false;
  }
  std::vector<std::uint8_t> body;
  body.reserve(kMaximumCacheFileSize);
  body.insert(body.end(), kMagic.begin(), kMagic.end());
  appendInteger(body, static_cast<std::uint32_t>(avatars.size()));
  for (const auto &avatar : avatars) {
    appendInteger(body, avatar.key.primary);
    appendInteger(body, avatar.key.secondary);
    appendInteger(body, static_cast<std::uint64_t>(avatar.stored_at));
    appendInteger(body, avatar.size);
    body.insert(body.end(), avatar.rgba.begin(), avatar.rgba.end());
  }
  if (body.size() > kMaximumCacheFileSize) {
    return false;
  }
  Handle file = 0;
  if (R_FAILED(FSUSER_OpenFile(&file, archive_, fsMakePath(PATH_ASCII, temporary_path_.c_str()),
                               FS_OPEN_WRITE | FS_OPEN_CREATE, 0)) ||
      R_FAILED(FSFILE_SetSize(file, 0))) {
    if (file != 0) {
      FSFILE_Close(file);
    }
    return false;
  }
  u32 bytes_written = 0;
  const auto result = FSFILE_Write(file, &bytes_written, 0, body.data(),
                                   static_cast<u32>(body.size()), FS_WRITE_FLUSH);
  FSFILE_Close(file);
  if (R_FAILED(result) || bytes_written != body.size()) {
    (void)FSUSER_DeleteFile(archive_, fsMakePath(PATH_ASCII, temporary_path_.c_str()));
    return false;
  }
  (void)FSUSER_DeleteFile(archive_, fsMakePath(PATH_ASCII, backup_path_.c_str()));
  (void)FSUSER_RenameFile(archive_, fsMakePath(PATH_ASCII, cache_path_.c_str()), archive_,
                          fsMakePath(PATH_ASCII, backup_path_.c_str()));
  if (R_FAILED(FSUSER_RenameFile(archive_, fsMakePath(PATH_ASCII, temporary_path_.c_str()),
                                 archive_, fsMakePath(PATH_ASCII, cache_path_.c_str())))) {
    (void)FSUSER_RenameFile(archive_, fsMakePath(PATH_ASCII, backup_path_.c_str()), archive_,
                            fsMakePath(PATH_ASCII, cache_path_.c_str()));
    (void)FSUSER_DeleteFile(archive_, fsMakePath(PATH_ASCII, temporary_path_.c_str()));
    return false;
  }
  (void)FSUSER_DeleteFile(archive_, fsMakePath(PATH_ASCII, backup_path_.c_str()));
  return true;
}

} // namespace office3ds::platform_3ds
