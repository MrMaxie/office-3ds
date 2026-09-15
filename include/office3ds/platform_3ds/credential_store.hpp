#pragma once

#include "office3ds/platform/credential.hpp"

#include <string>

#include <3ds.h>

namespace office3ds::platform_3ds {

class SdCredentialStore final : public platform::CredentialStore {
public:
  explicit SdCredentialStore(std::string product_slug);
  ~SdCredentialStore() override;

  [[nodiscard]] bool initialize();
  void shutdown() noexcept;
  [[nodiscard]] std::optional<core::CredentialBundle> load() override;
  [[nodiscard]] bool save(const core::CredentialBundle &credential) override;
  void clear() noexcept override;

private:
  std::string directory_;
  std::string credential_path_;
  std::string temporary_path_;
  std::string backup_path_;
  bool initialized_ = false;
  FS_Archive archive_{};
};

} // namespace office3ds::platform_3ds
