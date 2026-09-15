#include "office3ds/bridge_host/credential_source.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

namespace {

class TemporaryDirectory {
public:
  TemporaryDirectory() : path_(make_path()) { std::filesystem::create_directories(path_); }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  const std::filesystem::path &path() const { return path_; }

private:
  static std::filesystem::path make_path() {
    std::random_device random;
    return std::filesystem::temp_directory_path() /
           ("office-3ds-credential-source-" + std::to_string(random()));
  }

  std::filesystem::path path_;
};

void write_file(const std::filesystem::path &path, std::string_view contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
}

std::string base64url(std::string_view input) {
  constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string output;
  std::uint32_t buffer = 0;
  unsigned bits = 0;
  for (const unsigned char value : input) {
    buffer = (buffer << 8U) | value;
    bits += 8U;
    while (bits >= 6U) {
      bits -= 6U;
      output.push_back(alphabet[(buffer >> bits) & 0x3fU]);
    }
  }
  if (bits != 0U) {
    output.push_back(alphabet[(buffer << (6U - bits)) & 0x3fU]);
  }
  return output;
}

} // namespace

int main() {
  constexpr std::int64_t now = 1800000000;
  TemporaryDirectory temporary;

  const auto bundle_path = std::filesystem::absolute(temporary.path() / "bundle.json");
  write_file(
    bundle_path,
    nlohmann::json{{"access_token", "opaque-demo-token"}, {"expires_at", now + 3600}}.dump() +
      "\n");
  const auto bundle =
    office3ds::bridge_host::load_credential_file(bundle_path, "token_expiry", now);
  assert(bundle && bundle->access_token == "opaque-demo-token" &&
         bundle->expires_at_epoch_seconds == now + 3600);

  const auto jwt_path = std::filesystem::absolute(temporary.path() / "credential.jwt");
  const auto jwt = base64url(R"({"alg":"RS256"})") + "." +
                   base64url(nlohmann::json{{"exp", now + 7200}}.dump()) + ".signature";
  write_file(jwt_path, jwt);
  const auto parsed_jwt = office3ds::bridge_host::load_credential_file(jwt_path, "jwt_exp", now);
  assert(parsed_jwt && parsed_jwt->access_token == jwt &&
         parsed_jwt->expires_at_epoch_seconds == now + 7200);

  write_file(jwt_path, base64url("{}") + "." + base64url(nlohmann::json{{"exp", now - 1}}.dump()) +
                         ".signature");
  assert(!office3ds::bridge_host::load_credential_file(jwt_path, "jwt_exp", now));
  assert(!office3ds::bridge_host::load_credential_file(bundle_path, "unknown", now));
  assert(
    !office3ds::bridge_host::load_credential_file(bundle_path.filename(), "token_expiry", now));

  const auto oversized = std::filesystem::absolute(temporary.path() / "oversized.txt");
  write_file(oversized, std::string(33U * 1024U, 'x'));
  assert(!office3ds::bridge_host::load_credential_file(oversized, "jwt_exp", now));
  return 0;
}
