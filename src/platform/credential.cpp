#include "office3ds/platform/credential.hpp"

#include <string_view>

namespace office3ds::platform {
namespace {

bool containsControl(std::string_view value) {
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte <= 0x20U || byte == 0x7FU) {
      return true;
    }
  }
  return false;
}

bool validOrigin(std::string_view origin) {
  if (origin.empty() || origin.size() > CredentialPolicy::maximum_origin_size ||
      origin.back() == '/' || containsControl(origin) ||
      origin.find('?') != std::string_view::npos || origin.find('#') != std::string_view::npos) {
    return false;
  }
  return origin.rfind("https://", 0) == 0 || origin.rfind("http://", 0) == 0;
}

} // namespace

bool CredentialPolicy::valid_api_origin(std::string_view api_origin) {
  return validOrigin(api_origin);
}

CredentialStatus CredentialPolicy::evaluate(const core::CredentialBundle &credential,
                                            std::int64_t now_epoch_seconds) {
  if (credential.access_token.empty() || credential.access_token.size() > maximum_bearer_size ||
      containsControl(credential.access_token) || credential.expires_at_epoch_seconds <= 0) {
    return CredentialStatus::invalid;
  }
  return core::is_credential_usable(credential, now_epoch_seconds) ? CredentialStatus::usable
                                                                   : CredentialStatus::expired;
}

} // namespace office3ds::platform
