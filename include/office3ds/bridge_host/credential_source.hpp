#pragma once

#include "office3ds/core/credential_bundle.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace office3ds::bridge_host {

[[nodiscard]] std::optional<core::CredentialBundle>
load_credential_file(const std::filesystem::path &path, std::string_view expiry_policy,
                     std::int64_t now_epoch_seconds);

} // namespace office3ds::bridge_host
