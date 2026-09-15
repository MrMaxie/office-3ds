#pragma once

#include "office3ds/api/version.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace office3ds::api {

struct ProductDescriptor {
  std::uint32_t api_version = OFFICE_3DS_PRODUCT_API_VERSION;
  std::string slug;
  std::string display_name;
  std::string description;
  std::string author;
  std::string output_basename;
  std::string title_id;
  std::string product_code;
  std::string api_base_url;
  std::string token_file_environment;
  std::string credential_expiry_policy;
};

struct ProductPresentation {
  std::unordered_map<std::string, std::string> palette;
  std::unordered_map<std::string, std::string> copy;
  std::unordered_map<std::string, std::string> assets;
};

} // namespace office3ds::api
