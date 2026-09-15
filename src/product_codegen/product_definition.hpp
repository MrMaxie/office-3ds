#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace office3ds::codegen {

struct Operation {
  std::string name;
  std::string method;
  std::string path;
  std::string query;
  std::string headers;
  std::string body;
  std::string response;
  std::vector<std::string> depends_on;
};

struct ProductDefinition {
  unsigned schema_version = 0;
  unsigned api_version = 0;
  std::string slug;
  std::string display_name;
  std::string description;
  std::string author;
  std::string output_basename;
  std::string title_id;
  std::string product_code;
  std::string api_base_url;
  std::string token_file_environment;
  std::string expiry_policy;
  std::string adapter;
  std::map<std::string, std::string> palette;
  std::map<std::string, std::string> copy;
  std::map<std::string, std::string> assets;
  std::vector<std::filesystem::path> adapter_sources;
  std::vector<Operation> operations;
};

} // namespace office3ds::codegen
