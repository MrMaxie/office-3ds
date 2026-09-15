#include "office3ds/platform_3ds/credential_store.hpp"

#include "office3ds/core/credential_bundle.hpp"

#include <algorithm>
#include <array>
#include <ctime>

namespace office3ds::platform_3ds {
namespace {

constexpr u64 kMaximumCredentialFileSize = 32ULL * 1024ULL;

std::optional<std::string> read_file(FS_Archive archive, const std::string &path) {
  Handle file = 0;
  if (R_FAILED(
        FSUSER_OpenFile(&file, archive, fsMakePath(PATH_ASCII, path.c_str()), FS_OPEN_READ, 0))) {
    return std::nullopt;
  }
  u64 size = 0;
  if (R_FAILED(FSFILE_GetSize(file, &size)) || size == 0 || size > kMaximumCredentialFileSize) {
    FSFILE_Close(file);
    return std::nullopt;
  }
  std::string body(static_cast<std::size_t>(size), '\0');
  u32 bytes_read = 0;
  const auto result = FSFILE_Read(file, &bytes_read, 0, body.data(), static_cast<u32>(body.size()));
  FSFILE_Close(file);
  return R_SUCCEEDED(result) && bytes_read == body.size() ? std::optional{std::move(body)}
                                                          : std::nullopt;
}

bool valid_slug(std::string_view slug) {
  return !slug.empty() && slug.size() <= 64 &&
         slug.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string_view::npos;
}

} // namespace

SdCredentialStore::SdCredentialStore(std::string product_slug) {
  if (valid_slug(product_slug)) {
    directory_ = "/3ds/" + product_slug;
    credential_path_ = directory_ + "/credential.json";
    temporary_path_ = directory_ + "/credential.tmp";
    backup_path_ = directory_ + "/credential.bak";
  }
}

SdCredentialStore::~SdCredentialStore() { shutdown(); }

bool SdCredentialStore::initialize() {
  if (initialized_) {
    return true;
  }
  if (directory_.empty() || R_FAILED(fsInit())) {
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

void SdCredentialStore::shutdown() noexcept {
  if (initialized_) {
    FSUSER_CloseArchive(archive_);
    archive_ = {};
    fsExit();
    initialized_ = false;
  }
}

std::optional<core::CredentialBundle> SdCredentialStore::load() {
  if (!initialized_) {
    return std::nullopt;
  }
  auto body = read_file(archive_, credential_path_);
  if (!body.has_value()) {
    body = read_file(archive_, backup_path_);
    if (body.has_value()) {
      (void)FSUSER_DeleteFile(archive_, fsMakePath(PATH_ASCII, credential_path_.c_str()));
      (void)FSUSER_RenameFile(archive_, fsMakePath(PATH_ASCII, backup_path_.c_str()), archive_,
                              fsMakePath(PATH_ASCII, credential_path_.c_str()));
    }
  }
  if (!body.has_value()) {
    return std::nullopt;
  }
  auto credential = core::parse_credential_bundle(*body, std::time(nullptr));
  if (!credential.has_value()) {
    clear();
  }
  return credential;
}

bool SdCredentialStore::save(const core::CredentialBundle &credential) {
  if (!initialized_) {
    return false;
  }
  const auto body = core::serialize_credential_bundle(credential, std::time(nullptr));
  if (!body.has_value()) {
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
  const auto result = FSFILE_Write(file, &bytes_written, 0, body->data(),
                                   static_cast<u32>(body->size()), FS_WRITE_FLUSH);
  FSFILE_Close(file);
  if (R_FAILED(result) || bytes_written != body->size()) {
    (void)FSUSER_DeleteFile(archive_, fsMakePath(PATH_ASCII, temporary_path_.c_str()));
    return false;
  }
  (void)FSUSER_DeleteFile(archive_, fsMakePath(PATH_ASCII, backup_path_.c_str()));
  (void)FSUSER_RenameFile(archive_, fsMakePath(PATH_ASCII, credential_path_.c_str()), archive_,
                          fsMakePath(PATH_ASCII, backup_path_.c_str()));
  if (R_FAILED(FSUSER_RenameFile(archive_, fsMakePath(PATH_ASCII, temporary_path_.c_str()),
                                 archive_, fsMakePath(PATH_ASCII, credential_path_.c_str())))) {
    (void)FSUSER_RenameFile(archive_, fsMakePath(PATH_ASCII, backup_path_.c_str()), archive_,
                            fsMakePath(PATH_ASCII, credential_path_.c_str()));
    return false;
  }
  (void)FSUSER_DeleteFile(archive_, fsMakePath(PATH_ASCII, backup_path_.c_str()));
  return true;
}

void SdCredentialStore::clear() noexcept {
  if (!initialized_) {
    return;
  }
  for (const auto *path : {&credential_path_, &temporary_path_, &backup_path_}) {
    (void)FSUSER_DeleteFile(archive_, fsMakePath(PATH_ASCII, path->c_str()));
  }
}

} // namespace office3ds::platform_3ds
